#include <chrono>
#include <utility>

#include <quill/LogMacros.h>

#include "utils/task_runner.hpp"

namespace Omni {

SerialTaskRunner::SerialTaskRunner(quill::Logger* logger, std::string name, int max_queued)
:   logger_(logger),
    name_(std::move(name)),
    max_queued_(max_queued > 0 ? max_queued : 1)
{
    worker_ = std::thread([this]() { loop(); });
}


SerialTaskRunner::~SerialTaskRunner() {
    // Everything already queued is run, not discarded. These are real requests the
    // caller was told went out -- dropping a queued cancel here would leave an order
    // resting at the venue while the process that placed it exits, which is precisely
    // the outcome the trader's shutdown exists to prevent.
    drain();

    running_.store(false, std::memory_order_release);
    // The worker wakes from its timed wait on its own; nothing to signal.
    if (worker_.joinable()) worker_.join();
}


bool SerialTaskRunner::post(std::function<void()> task) {
    if (outstanding_.fetch_add(1, std::memory_order_acq_rel) >= max_queued_) {
        outstanding_.fetch_sub(1, std::memory_order_acq_rel);
        LOG_WARNING(
            logger_, "{}: {} requests already queued; refusing a new one",
            name_, max_queued_
        );
        return false;
    }
    queue_.enqueue(std::move(task));
    return true;
}


void SerialTaskRunner::loop() {
    std::function<void()> task;
    while (true) {
        // Timed, so the worker notices a stop even with nothing left to do. It keeps
        // draining after running_ clears: the destructor only lowers the flag once
        // outstanding_ is already zero, so anything still here has to be run.
        if (!queue_.wait_dequeue_timed(task, std::chrono::milliseconds(50))) {
            if (!running_.load(std::memory_order_acquire)) return;
            continue;
        }

        {
            // Moved into the inner scope so the callable (and everything it captured)
            // is destroyed *before* the counter drops. drain() returns the moment the
            // counter hits zero, and what the task captured may be owned by the object
            // being torn down.
            auto work = std::move(task);
            try {
                work();
            } catch (const std::exception& e) {
                LOG_WARNING(logger_, "{}: task threw: {}", name_, e.what());
            } catch (...) {
                LOG_WARNING(logger_, "{}: task threw a non-standard exception", name_);
            }
        }
        outstanding_.fetch_sub(1, std::memory_order_acq_rel);
    }
}


void SerialTaskRunner::drain() {
    using namespace std::chrono;
    auto start = steady_clock::now();
    bool warned = false;

    while (outstanding_.load(std::memory_order_acquire) > 0) {
        // Unbounded on purpose. Giving up early would let the worker call back into an
        // object halfway through being destroyed, and a use-after-free is a far worse
        // outcome than a slow shutdown. curl's timeout is what bounds each task, and
        // the backlog cap bounds how many there can be.
        if (!warned && steady_clock::now() - start > seconds(1)) {
            LOG_WARNING(
                logger_, "{}: waiting on {} queued request(s) before shutting down",
                name_, outstanding_.load(std::memory_order_acquire)
            );
            warned = true;
        }
        std::this_thread::sleep_for(milliseconds(1));
    }
}

} // namespace Omni

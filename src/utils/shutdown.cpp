#include <atomic>
#include <csignal>

#include "utils/shutdown.hpp"

namespace Omni {

namespace {

// Read and written from a signal handler, so it must be lock-free: a handler that
// blocked on a mutex the interrupted thread already holds would deadlock the
// process. Nothing else in here allocates, locks or logs, for the same reason.
std::atomic<bool> g_shutdown_requested{false};
static_assert(
    std::atomic<bool>::is_always_lock_free,
    "the shutdown flag is set from a signal handler and must be lock-free"
);

extern "C" void on_shutdown_signal(int sig) {
    if (g_shutdown_requested.exchange(true, std::memory_order_relaxed)) {
        // Already shutting down and the operator asked again. Put the default
        // disposition back and re-raise, so this signal kills the process the way it
        // would have if we had never installed a handler.
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    }
}

} // namespace


void install_shutdown_handler() {
    std::signal(SIGINT, on_shutdown_signal);
    std::signal(SIGTERM, on_shutdown_signal);
}


bool shutdown_requested() {
    return g_shutdown_requested.load(std::memory_order_relaxed);
}


void request_shutdown() {
    g_shutdown_requested.store(true, std::memory_order_relaxed);
}

} // namespace Omni

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include <moodycamel/blockingconcurrentqueue.h>
#include <quill/Logger.h>

namespace Omni {

// A single worker thread that runs queued work one piece at a time, in the order it
// was posted.
//
// It exists for the REST order gateways. Their HTTP calls are synchronous -- curl,
// bounded only by CURLOPT_TIMEOUT -- and they used to run on the trader thread, so a
// slow or hung exchange stalled every decision behind them. Posting the request here
// makes place/amend/cancel return immediately, which is what the IOrderGateway
// contract promises.
//
// One worker, not a thread per request, and the ordering is the reason. The trader
// issues a cancel and its replacement in one breath, and expects the venue to see
// them that way: a replace whose place overtook its cancel rests at both prices at
// once and briefly doubles the size on that side. Concurrent requests would be faster
// in aggregate but would give up exactly the guarantee that makes cancel-then-place
// safe.
//
// The cost is that requests are serialized: N orders take N round trips end to end,
// where a thread each would have overlapped them. That is the deliberate trade -- the
// thing being fixed is the trader thread blocking, not the wall-clock time of a burst.
class SerialTaskRunner {
    public:
        // `max_queued` caps how much can be waiting, so a venue that stops answering
        // cannot let the backlog grow without bound.
        SerialTaskRunner(quill::Logger* logger, std::string name, int max_queued = 64);

        // Runs everything still queued, then joins the worker. A posted task calling
        // back into an object that is being destroyed is exactly what this prevents.
        ~SerialTaskRunner();

        SerialTaskRunner(const SerialTaskRunner&) = delete;
        SerialTaskRunner& operator=(const SerialTaskRunner&) = delete;

        // Queue one task. FIFO: it runs after everything posted before it, and before
        // everything posted after. Returns false only if it could not be queued at all
        // (backlog full). The caller is expected to report that as a failed request
        // rather than drop it silently, so the owner's bookkeeping does not sit waiting
        // on a reply that will never come.
        bool post(std::function<void()> task);

        // Block until the queue is empty and the worker is idle. Terminates because
        // every task this is used for is a REST call under a hard curl timeout.
        //
        // Never call this from the worker itself, and note that a class whose tasks
        // call its virtual methods must drain in *its own* destructor: by the time a
        // base destructor runs, the derived part is already gone.
        void drain();

        int pending() const { return outstanding_.load(std::memory_order_acquire); }

    private:
        void loop();

        quill::Logger* logger_;
        std::string name_;
        int max_queued_;

        // Queued but not yet finished, counting the one currently running. drain()
        // waits on this reaching zero.
        std::atomic<int> outstanding_{0};
        std::atomic<bool> running_{true};

        moodycamel::BlockingConcurrentQueue<std::function<void()>> queue_;
        std::thread worker_;
};

} // namespace Omni

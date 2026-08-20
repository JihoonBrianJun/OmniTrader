#include <atomic>
#include <csignal>
#include <ctime>
#include <unistd.h>

#include "utils/shutdown.hpp"

namespace Omni {

namespace {

// Read and written from a signal handler, so these must be lock-free: a handler that
// blocked on a mutex the interrupted thread already holds would deadlock the
// process. Nothing in here allocates, locks or logs, for the same reason -- write(2)
// and clock_gettime(2) are on the async-signal-safe list, quill's frontend is not.
std::atomic<bool> g_shutdown_requested{false};
std::atomic<long> g_first_signal_sec{0};

static_assert(
    std::atomic<bool>::is_always_lock_free && std::atomic<long>::is_always_lock_free,
    "the shutdown state is set from a signal handler and must be lock-free"
);


long monotonic_sec() {
    struct timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<long>(ts.tv_sec);
}


// The console is the only place an operator can see any of this: the trader logs to a
// file, so without these lines Ctrl-C looks like a hang, and a second press follows.
void say(const char* msg) {
    size_t len = 0;
    while (msg[len] != '\0') ++len;
    ssize_t written = 0;
    while (written < static_cast<ssize_t>(len)) {
        ssize_t n = ::write(STDERR_FILENO, msg + written, len - static_cast<size_t>(written));
        if (n <= 0) return;   // nothing useful to do about a failed write in here
        written += n;
    }
}


extern "C" void on_shutdown_signal(int sig) {
    if (!g_shutdown_requested.exchange(true, std::memory_order_relaxed)) {
        g_first_signal_sec.store(monotonic_sec(), std::memory_order_relaxed);
        say("\n[trader] stopping: cancelling resting orders and flattening. "
            "This takes a few seconds -- press Ctrl-C again after that to give up "
            "and leave them live.\n");
        return;
    }

    // Already shutting down and the operator pressed again.
    long since = monotonic_sec() - g_first_signal_sec.load(std::memory_order_relaxed);
    if (since < SHUTDOWN_GRACE_SEC) {
        // Almost certainly the same keystroke twice. Killing here would abandon the
        // cancel sequence with orders still working at the exchange.
        say("[trader] shutdown already running; ignoring. Press again in a few "
            "seconds to force (orders may be left live).\n");
        return;
    }

    say("[trader] forced: exiting without finishing the shutdown. "
        "Check the exchange for live orders and open positions.\n");
    std::signal(sig, SIG_DFL);
    std::raise(sig);
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
    if (!g_shutdown_requested.exchange(true, std::memory_order_relaxed)) {
        g_first_signal_sec.store(monotonic_sec(), std::memory_order_relaxed);
    }
}

} // namespace Omni

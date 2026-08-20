#pragma once

namespace Omni {

// Process-wide "please stop" flag, raised from a signal handler.
//
// A live trader is normally stopped by a signal: Ctrl-C at the console, a
// supervisor's SIGTERM, a plain `kill`. None of those may simply drop the process,
// because whatever it had resting at the exchange stays resting and whatever it was
// holding stays held. The handler therefore does the only thing that is safe in a
// signal context -- set a flag and write one line to stderr -- and the trader's own
// loop notices it and runs the shutdown sequence on the trader thread, where it can
// actually talk to the gateway.
//
// A *repeat* signal is handled by the clock, not by a counter, and the reason is a
// habit: the trader logs to a file, so Ctrl-C produces no visible reaction, and the
// natural response is to press it again within a fraction of a second. Killing on
// that second press would abandon the cancel sequence a hundred milliseconds in and
// leave live orders at the exchange -- precisely what the sequence exists to prevent.
// So a repeat inside the grace window below is reported and ignored, while one after
// it restores the default disposition and re-raises, keeping the escape hatch for an
// operator whose shutdown really is hanging. That leaves orders behind, which is
// their call to make once they have actually waited.
void install_shutdown_handler();

// Seconds after the first signal during which a repeat will not kill the process.
// Long enough to cover a normal shutdown (a cancel round trip plus the flatten), and
// far longer than a reflex double-tap.
inline constexpr long SHUTDOWN_GRACE_SEC = 5;

// True once a shutdown has been requested. Cheap enough to poll every loop turn.
bool shutdown_requested();

// Raise the flag without a signal (end-of-session, tests).
void request_shutdown();

} // namespace Omni

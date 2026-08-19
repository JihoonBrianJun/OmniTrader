#pragma once

namespace Omni {

// Process-wide "please stop" flag, raised from a signal handler.
//
// A live trader is normally stopped by a signal: Ctrl-C at the console, a
// supervisor's SIGTERM, a plain `kill`. None of those may simply drop the process,
// because whatever it had resting at the exchange stays resting and whatever it was
// holding stays held. The handler therefore does the only thing that is safe in a
// signal context -- set a flag -- and the trader's own loop notices it and runs the
// shutdown sequence on the trader thread, where it can actually talk to the gateway.
//
// A *second* signal is not swallowed: the handler restores the default disposition
// and re-raises, so an operator whose graceful shutdown is hanging can always stop
// the process the way they expect. That leaves orders behind, which is the operator's
// call to make, not ours.
void install_shutdown_handler();

// True once a shutdown has been requested. Cheap enough to poll every loop turn.
bool shutdown_requested();

// Raise the flag without a signal (end-of-session, tests).
void request_shutdown();

} // namespace Omni

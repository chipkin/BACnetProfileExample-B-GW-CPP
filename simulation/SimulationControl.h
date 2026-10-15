// SPDX-License-Identifier: CC0-1.0
// Public-domain example code (CC0) - see ../LICENSE.
#ifndef SIMULATION_CONTROL_H
#define SIMULATION_CONTROL_H

// =============================================================================
// SIMULATION ONLY, TEST ONLY - the back door the Wire Tests (tests/) use to
// drive the simulated foreign bus from outside the process: pull a slave's
// cable, change a sensor reading, look inside a register.
//
// It is OFF unless the environment variable BGW_SIMULATION_CONTROL_PORT names
// a UDP port. Then it listens on 127.0.0.1 (loopback only) on that port for
// one plain-text command per datagram and answers each with one datagram:
//
//     offline <unit>                          -> ok
//     online <unit>                           -> ok
//     set <unit> input|holding|coil <addr> <value>   -> ok
//     get <unit> input|holding|coil <addr>    -> value <n>
//     (anything else)                         -> error <reason>
//
// It is deliberately NOT a command-line option: nothing here changes what
// the gateway does on the BACnet network, and a customer reading --help
// should never meet it. A real product has no such thing - delete this file
// together with simulation/SimulatedForeignBus.cpp.
// =============================================================================

#include <stdint.h>

namespace SimulationControl {

// Opens the control socket if BGW_SIMULATION_CONTROL_PORT is set. Call after
// common/'s SetupUDP (which has already initialised sockets on Windows).
// Returns false only if the variable is set and the socket could not be opened.
bool Start();

// Answer any commands waiting on the socket. Non-blocking; call every loop.
void Service();

void Stop();

} // namespace SimulationControl

#endif // SIMULATION_CONTROL_H

// SPDX-License-Identifier: CC0-1.0
// Public-domain example code (CC0) - see LICENSE.
#ifndef FOREIGN_BUS_H
#define FOREIGN_BUS_H

// =============================================================================
// ForeignBus.h - THE SEAM between this BACnet gateway and the non-BACnet network
// it gateways for.
//
//   +------------------+        +-------------------+        +------------------+
//   |  BACnet network  | <----> |  main.cpp         | <----> |  ForeignBus      |
//   |  (CAS BACnet     |        |  (the gateway:    |        |  (THIS interface)|
//   |   Stack)         |        |   objects, cache, |        |                  |
//   +------------------+        |   poll scheduler) |        +--------+---------+
//                               +-------------------+                 |
//                                                      simulation/SimulatedForeignBus.cpp
//                                                      (in this example) - or YOUR driver
//
// In this example the implementation is SIMULATION: simulation/
// SimulatedForeignBus.cpp keeps three pretend field devices ("slaves") in
// memory, each with a small Modbus-style register map, its own response
// latency and a slowly drifting sensor value. Nothing leaves the process.
//
// A REAL PRODUCT REPLACES simulation/SimulatedForeignBus.cpp WITH ITS OWN
// DRIVER that implements these same functions over the real wire - Modbus
// TCP or Modbus RTU, M-Bus, a PLC's native protocol (EtherNet/IP, S7,
// MELSEC...), a vendor's serial protocol, a REST API - anything BACnet-unaware.
// main.cpp does not change: it only ever talks to the functions below.
//
// The interface is deliberately the shape of a typical field-bus MASTER
// driver: ASYNCHRONOUS. You start a transaction, keep running, and check on it
// later. The answer either arrives (after the device's latency) or the
// transaction times out because the device is offline. A BACnet device must
// never block waiting for a slow foreign device (Annex H.2.1.4), so a
// synchronous "read and wait" driver is the wrong shape for a gateway.
//
// Addressing follows Modbus terms because they are the most familiar: a slave
// is a unit id (1..247), and each slave has three tables addressed from 0.
// A non-Modbus driver maps its own addressing onto (unit, table, address).
// =============================================================================

#include <stdint.h>

namespace ForeignBus {

// The three kinds of foreign data point this gateway maps (Modbus names).
enum class Table {
    InputRegister,   // read-only 16-bit value measured by the field device (a sensor)
    HoldingRegister, // read/write 16-bit value (a setpoint the gateway commands)
    Coil             // read/write single bit (an on/off command)
};

// What happened to a transaction.
enum class Outcome {
    Pending,  // still on the wire - check again later
    Done,     // the field device answered; for a read, *value holds the answer
    Timeout   // no answer within the driver's response timeout (device offline?)
};

// Start the driver (open the port / connect / build the simulation).
void Start();

// Give the driver CPU time. Call it on every pass of the main loop, the same
// way BACnetStack_Tick() is called. nowMilliseconds is a monotonic clock.
void Service(uint64_t nowMilliseconds);

// Start reading one data point. Returns a transaction id (never 0) to pass to
// CheckTransaction, or 0 if the request could not even be started.
uint32_t StartRead(uint8_t unitId, Table table, uint16_t address);

// Start writing one data point (a holding register or a coil; a coil takes 0 or 1).
uint32_t StartWrite(uint8_t unitId, Table table, uint16_t address, uint16_t value);

// How is the transaction doing? Once it reports Done or Timeout the id is
// finished and must not be checked again.
Outcome CheckTransaction(uint32_t transactionId, uint16_t* value);

// The name of the table, for log lines.
const char* TableName(Table table);

} // namespace ForeignBus

#endif // FOREIGN_BUS_H

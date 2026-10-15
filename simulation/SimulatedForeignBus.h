// SPDX-License-Identifier: CC0-1.0
// Public-domain example code (CC0) - see ../LICENSE.
#ifndef SIMULATED_FOREIGN_BUS_H
#define SIMULATED_FOREIGN_BUS_H

// =============================================================================
// SIMULATION ONLY - not part of the gateway, and not part of the BACnet Surface.
//
// simulation/SimulatedForeignBus.cpp implements ForeignBus.h (the seam) with
// three pretend field devices held in memory. The functions below reach
// INSIDE that simulation - to pull a slave's cable, or to change a value the
// way the field device itself would - so the gateway's behaviour can be shown
// (the 'k' key) and tested (simulation/SimulationControl.cpp). A real driver
// has nothing like them: on a real bus a slave goes offline by itself.
// =============================================================================

#include "../ForeignBus.h"

#include <stdint.h>

namespace SimulatedForeignBus {

// Unit ids of the simulated slaves: 1, 2 and 3.
static const uint8_t FIRST_UNIT_ID = 1;
static const uint8_t SLAVE_COUNT = 3;

// The register map every simulated slave has. Values are 16-bit, temperatures
// scaled x10 (215 = 21.5 C), the usual Modbus convention.
static const uint16_t INPUT_REGISTER_MEASURED_TEMPERATURE = 0; // read-only sensor, drifts
static const uint16_t HOLDING_REGISTER_SETPOINT = 0;           // read/write setpoint
static const uint16_t COIL_ENABLE = 0;                         // read/write on/off command

// The slave's name ("Boiler", ...) or NULL for an unknown unit id.
const char* SlaveName(uint8_t unitId);

// Pull (online = false) or restore (true) a slave's connection. An offline
// slave answers nothing, so every transaction to it ends in Timeout.
bool SetSlaveOnline(uint8_t unitId, bool online);
bool IsSlaveOnline(uint8_t unitId);

// Change a data point the way the FIELD DEVICE would (a sensor reading moves,
// a local operator changes a setpoint). Setting the input register also stops
// that slave's automatic drift, so a test sees exactly the value it set.
bool SetPoint(uint8_t unitId, ForeignBus::Table table, uint16_t address, uint16_t value);

// Read a data point straight out of the field device - what is REALLY in the
// register, as opposed to what the gateway believes is there.
bool GetPoint(uint8_t unitId, ForeignBus::Table table, uint16_t address, uint16_t* value);

} // namespace SimulatedForeignBus

#endif // SIMULATED_FOREIGN_BUS_H

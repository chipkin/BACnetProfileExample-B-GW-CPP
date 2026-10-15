// SPDX-License-Identifier: CC0-1.0
// Public-domain example code (CC0) - see ../LICENSE.
// =============================================================================
// SimulatedForeignBus.cpp - SIMULATION. Implements the ForeignBus.h seam with
// three pretend field devices ("slaves") held in memory.
//
// A REAL PRODUCT DELETES THIS FILE and implements ForeignBus.h with its own
// driver - Modbus TCP/RTU, a PLC protocol, a vendor serial protocol, ... -
// see ForeignBus.h. Nothing in here is BACnet, and nothing in here is part
// of what this example shows a BACnet client.
//
// What is simulated, so the gateway has something real-looking to deal with:
//
//   - Three slaves, each with the same small register map (see
//     SimulatedForeignBus.h): an input register (a temperature sensor), a
//     holding register (a setpoint) and a coil (an enable).
//   - LATENCY: every slave takes a while to answer (its own, fixed delay).
//   - DRIFT: every slave's sensor wanders +/-0.5 C around its starting value,
//     one step a second, until something sets it explicitly.
//   - OFFLINE: a slave whose connection is pulled answers nothing; the
//     transaction ends in Timeout after RESPONSE_TIMEOUT_MILLISECONDS, exactly
//     as a real master driver reports a dead slave.
// =============================================================================

#include "SimulatedForeignBus.h"

#include <map>

namespace {

// How long a real master driver would wait for an answer before giving up.
const uint64_t RESPONSE_TIMEOUT_MILLISECONDS = 300;

// How often each slave's sensor drifts by one step (0.1 C), and how far.
const uint64_t DRIFT_STEP_MILLISECONDS = 1000;
const int DRIFT_MAXIMUM_STEPS = 5;

struct SimulatedSlave {
    uint8_t unitId;
    const char* name;
    uint64_t latencyMilliseconds;   // how long this device takes to answer
    bool online;
    uint16_t inputRegister;         // INPUT_REGISTER_MEASURED_TEMPERATURE
    uint16_t holdingRegister;       // HOLDING_REGISTER_SETPOINT
    uint16_t coil;                  // COIL_ENABLE (0 or 1)
    bool drifting;                  // false once something sets the sensor explicitly
    uint16_t driftCentre;
    int driftOffset;
    int driftDirection;
};

SimulatedSlave g_slaves[SimulatedForeignBus::SLAVE_COUNT] = {
    // unit, name,          latency, online, sensor, setpoint, enable, drifting, centre, offset, direction
    { 1,     "Boiler",      20,      true,   652,    650,      1,      true,     652,    0,      1 },
    { 2,     "Air Handler", 45,      true,   131,    130,      1,      true,     131,    0,      1 },
    { 3,     "Chiller",     80,      true,   71,     70,       0,      true,     71,     0,      1 },
};

struct Transaction {
    uint8_t unitId;
    ForeignBus::Table table;
    uint16_t address;
    bool isWrite;
    uint16_t value;
    uint64_t startedAt;
    ForeignBus::Outcome outcome;
};

std::map<uint32_t, Transaction> g_transactions;
uint32_t g_nextTransactionId = 1;
uint64_t g_nowMilliseconds = 0;
uint64_t g_lastDriftAt = 0;

SimulatedSlave* FindSlave(uint8_t unitId) {
    for (SimulatedSlave& slave : g_slaves) {
        if (slave.unitId == unitId) {
            return &slave;
        }
    }
    return nullptr;
}

// The one register/coil a (table, address) names, or nullptr (this map has
// one point of each kind, at address 0).
uint16_t* FindPoint(SimulatedSlave* slave, ForeignBus::Table table, uint16_t address) {
    if (slave == nullptr || address != 0) {
        return nullptr;
    }
    switch (table) {
        case ForeignBus::Table::InputRegister:   return &slave->inputRegister;
        case ForeignBus::Table::HoldingRegister: return &slave->holdingRegister;
        case ForeignBus::Table::Coil:            return &slave->coil;
    }
    return nullptr;
}

uint32_t StartTransaction(uint8_t unitId, ForeignBus::Table table, uint16_t address,
                          bool isWrite, uint16_t value) {
    if (FindPoint(FindSlave(unitId), table, address) == nullptr) {
        return 0; // not in the register map - a real driver rejects this before it hits the wire
    }
    if (isWrite && table == ForeignBus::Table::InputRegister) {
        return 0; // input registers are read-only
    }
    const uint32_t transactionId = g_nextTransactionId++;
    if (g_nextTransactionId == 0) {
        g_nextTransactionId = 1;
    }
    g_transactions[transactionId] = { unitId, table, address, isWrite, value,
                                      g_nowMilliseconds, ForeignBus::Outcome::Pending };
    return transactionId;
}

void Drift() {
    for (SimulatedSlave& slave : g_slaves) {
        if (!slave.drifting) {
            continue;
        }
        if (slave.driftOffset + slave.driftDirection > DRIFT_MAXIMUM_STEPS ||
            slave.driftOffset + slave.driftDirection < -DRIFT_MAXIMUM_STEPS) {
            slave.driftDirection = -slave.driftDirection;
        }
        slave.driftOffset += slave.driftDirection;
        slave.inputRegister = (uint16_t)(slave.driftCentre + slave.driftOffset);
    }
}

} // namespace

// -----------------------------------------------------------------------------
// ForeignBus.h - the seam
// -----------------------------------------------------------------------------
namespace ForeignBus {

void Start() {
    // A real driver opens its serial port or TCP connections here.
}

void Service(uint64_t nowMilliseconds) {
    g_nowMilliseconds = nowMilliseconds;
    if (g_lastDriftAt == 0) {
        g_lastDriftAt = nowMilliseconds;
    }
    if (nowMilliseconds - g_lastDriftAt >= DRIFT_STEP_MILLISECONDS) {
        g_lastDriftAt = nowMilliseconds;
        Drift();
    }

    // Answer (or time out) every transaction whose moment has come.
    for (auto& entry : g_transactions) {
        Transaction& transaction = entry.second;
        if (transaction.outcome != Outcome::Pending) {
            continue;
        }
        SimulatedSlave* slave = FindSlave(transaction.unitId);
        const uint64_t elapsed = nowMilliseconds - transaction.startedAt;
        if (slave->online && elapsed >= slave->latencyMilliseconds) {
            uint16_t* point = FindPoint(slave, transaction.table, transaction.address);
            if (transaction.isWrite) {
                *point = (transaction.table == Table::Coil) ? (transaction.value ? 1 : 0) : transaction.value;
            } else {
                transaction.value = *point;
            }
            transaction.outcome = Outcome::Done;
        } else if (elapsed >= RESPONSE_TIMEOUT_MILLISECONDS) {
            transaction.outcome = Outcome::Timeout;
        }
    }
}

uint32_t StartRead(uint8_t unitId, Table table, uint16_t address) {
    return StartTransaction(unitId, table, address, false, 0);
}

uint32_t StartWrite(uint8_t unitId, Table table, uint16_t address, uint16_t value) {
    return StartTransaction(unitId, table, address, true, value);
}

Outcome CheckTransaction(uint32_t transactionId, uint16_t* value) {
    auto found = g_transactions.find(transactionId);
    if (found == g_transactions.end()) {
        return Outcome::Timeout;
    }
    const Outcome outcome = found->second.outcome;
    if (outcome != Outcome::Pending) {
        if (outcome == Outcome::Done && value != nullptr) {
            *value = found->second.value;
        }
        g_transactions.erase(found);
    }
    return outcome;
}

const char* TableName(Table table) {
    switch (table) {
        case Table::InputRegister:   return "input register";
        case Table::HoldingRegister: return "holding register";
        case Table::Coil:            return "coil";
    }
    return "?";
}

} // namespace ForeignBus

// -----------------------------------------------------------------------------
// SimulatedForeignBus.h - reaching inside the simulation
// -----------------------------------------------------------------------------
namespace SimulatedForeignBus {

const char* SlaveName(uint8_t unitId) {
    const SimulatedSlave* slave = FindSlave(unitId);
    return slave ? slave->name : nullptr;
}

bool SetSlaveOnline(uint8_t unitId, bool online) {
    SimulatedSlave* slave = FindSlave(unitId);
    if (slave == nullptr) {
        return false;
    }
    slave->online = online;
    return true;
}

bool IsSlaveOnline(uint8_t unitId) {
    const SimulatedSlave* slave = FindSlave(unitId);
    return slave != nullptr && slave->online;
}

bool SetPoint(uint8_t unitId, ForeignBus::Table table, uint16_t address, uint16_t value) {
    SimulatedSlave* slave = FindSlave(unitId);
    uint16_t* point = FindPoint(slave, table, address);
    if (point == nullptr) {
        return false;
    }
    *point = (table == ForeignBus::Table::Coil) ? (value ? 1 : 0) : value;
    if (table == ForeignBus::Table::InputRegister) {
        slave->drifting = false;
    }
    return true;
}

bool GetPoint(uint8_t unitId, ForeignBus::Table table, uint16_t address, uint16_t* value) {
    uint16_t* point = FindPoint(FindSlave(unitId), table, address);
    if (point == nullptr) {
        return false;
    }
    *value = *point;
    return true;
}

} // namespace SimulatedForeignBus

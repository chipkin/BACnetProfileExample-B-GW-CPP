// SPDX-License-Identifier: CC0-1.0
// Public-domain example code (CC0) - see LICENSE. The CAS BACnet Stack itself is
// a separate, commercially licensed product and is not covered by CC0.
// =============================================================================
// BACnet Profile Example - B-GW (Gateway) - C++
//
// A B-GW connects a non-BACnet network into a BACnet internetwork (135-2024
// Annex L.7.2). Its Profile Function, in one sentence:
//
//     The gateway polls devices on a foreign (non-BACnet) bus and presents
//     their data as BACnet objects - reads follow the foreign values, writes
//     are carried through to the foreign devices, and a foreign device that
//     stops answering is reported, not hidden.
//
// The foreign bus here is SIMULATION (simulation/SimulatedForeignBus.cpp):
// three pretend field devices ("slaves") with a Modbus-style register map,
// each with its own poll cycle, response latency and a drifting sensor. The
// gateway only ever talks to it through ForeignBus.h - THE SEAM. A real
// product replaces the simulation with its own driver behind the same seam:
// Modbus TCP or RTU, a PLC's native protocol, a vendor serial protocol...
//
// The register map (every slave has the same one; see SimulatedForeignBus.h):
//
//     unit  slave          input register 0      holding register 0     coil 0
//     1     Boiler         supply temperature    supply setpoint        burner enable
//     2     Air Handler    discharge temperature discharge setpoint     fan enable
//     3     Chiller        chilled-water temp.   chilled-water setpoint compressor enable
//     (temperatures are degrees Celsius x 10 - 652 = 65.2 C)
//
// Annex L.7.2: "There are two types of B-GW devices, those that represent
// non-BACnet devices as a network of virtual BACnet devices, and those that
// represent non-BACnet devices as BACnet objects." This one example is
// either, picked at start-up with --mode:
//
//   --mode eo  (the default) - GW-EO-B, Annex K.7.2 / H.1.1.3. Every slave's
//              data becomes objects INSIDE the gateway device:
//                  Analog Input  n  "Bronze n"      <- slave n input register 0
//                  Analog Output n  "Chartreuse n"  -> slave n holding register 0
//                  Binary Output n  "Fuchsia n"     -> slave n coil 0
//              (instance 1 is plain "Bronze" etc., per docs/colour-table.md).
//              A slave that stops answering: the three objects keep answering
//              reads from the cache, with Reliability = COMMUNICATION_FAILURE
//              and the FAULT flag set in Status_Flags (Annex H.2.3.2).
//
//   --mode vn  - GW-VN-B, Annex K.7.1 / H.1.1.1. Every slave becomes its own
//              VIRTUAL BACnet device on virtual network 100 behind the
//              gateway, which appears to the network as a router (NM-RC-B):
//                  Device 389119 + n - 1  "Chipkin Example B-GW (Boiler)" ...
//                      Analog Input 1 "Bronze", Analog Output 1 "Chartreuse",
//                      Binary Output 1 "Fuchsia" - mapped as above.
//              A slave that stops answering: its virtual device stops
//              answering Who-Is (no I-Am on its behalf - H.2.2.1). H.2.2.1
//              also requires the virtual device to stay SILENT to requests
//              directed to it; the stack has no customer export for that yet,
//              so reads are still answered (cached value, Reliability =
//              COMMUNICATION_FAILURE + FAULT flag). That gap is documented in
//              the README and TODO.md.
//
// BIBBs (Annex L.7.2): DS-RP-B, DS-WP-B, DM-DDB-B, DM-DOB-B, DM-DCC-B, plus
// GW-EO-B (--mode eo) or GW-VN-B and the NM-RC-B it requires (--mode vn).
//
// Outputs are COMMANDABLE with a full 16-slot Priority_Array (Annex H.2.1.3).
// The winning value is written through to the slave asynchronously: the
// WriteProperty gets its Result(+) at once, before the slave has the value
// (write caching, Annex H.2.1.1), and the gateway retries until it lands -
// including when an offline slave comes back.
//
// Interactive keys: h help, q quit, up/down nudge the SIMULATED Boiler's
// sensor by 1.1 C (Analog Input 1 follows on the next poll), k kill / revive
// the simulated Boiler (the Failure Mode), r re-send I-Am-Router-To-Network
// (--mode vn) or the gateway's I-Am (--mode eo).
// Command line: --mode eo|vn, --port <n>, --deviceID <n> (the gateway's
// instance; in --mode vn the virtual devices are fixed at 389119..389121).
// =============================================================================

#include "CASExampleHelper.h"
#include "CASBACnetStackExampleConstants.h"
#include "CASBACnetStackAdapter.h" // the CAS BACnet Stack C API (BACnetStack_*); call
                                    // LoadBACnetFunctions() before any BACnetStack_* call -
                                    // see the top of main() below.
#include "ForeignBus.h"                      // THE SEAM to the non-BACnet network
#include "simulation/SimulatedForeignBus.h"  // SIMULATION only: the 'k' and up/down keys
#include "simulation/SimulationControl.h"    // SIMULATION only: the Wire Tests' back door

#include <chrono>
#include <stdio.h>
#include <string.h>
#include <string>

#if defined(_WIN32)
#include <windows.h> // Sleep()
#else
#include <unistd.h>  // usleep()
#endif

using namespace CASBACnetStackExampleConstants;

// -----------------------------------------------------------------------------
// 1. Example + device configuration
// -----------------------------------------------------------------------------
static const char* APP_NAME = "BACnet B-GW (Gateway) Example - C++";
static const char* APP_VERSION = "2.0.0";

// Which kind of gateway this run is (Annex L.7.2 allows either).
enum class GatewayMode {
    EmbeddedObjects, // --mode eo: GW-EO-B, the slaves' data are objects in the gateway
    VirtualNetwork   // --mode vn: GW-VN-B, each slave is a virtual device on a virtual network
};
static GatewayMode g_mode = GatewayMode::EmbeddedObjects;

// The GATEWAY device instance. BACnet requires this to be configurable, so it
// defaults to 389019 (docs/colour-table.md) and can be overridden on the
// command line with --deviceID. Keep it configurable in your product: it
// must be unique across the internetwork.
static uint32_t g_deviceInstance = 389019;

// --mode vn: the virtual devices are 389119, 389120, 389121 - the gateway's
// number + 100 and up (docs/colour-table.md). NOT moved by --deviceID.
// CHANGE BEFORE YOU SHIP: each must be unique across the whole internetwork.
static const uint32_t FIRST_VIRTUAL_DEVICE_INSTANCE = 389119;

// --mode vn: the virtual network the virtual devices live on, and the Network
// Port object instance the stack creates for it. Not a real, wire-configured
// network - see BACnetStack_AddVirtualNetwork's doc comment.
// CHANGE BEFORE YOU SHIP: unique among every network number on the internetwork.
static const uint16_t VIRTUAL_NETWORK_NUMBER = 100;
static const uint32_t VIRTUAL_NETWORK_PORT_INSTANCE = 2; // "Network Port 1" (Vermilion) is 1

// ---- Device identity: CHANGE ALL OF THIS BEFORE YOU SHIP --------------------
// Everything in this block is read by clients and shown to the operator in every
// discovery tool on the network. Left as-is, your product will appear on a real
// site announcing itself as a Chipkin demo. None of it is cosmetic:
// Object_Name must be unique across the BACnet internetwork, and Model_Name /
// Vendor_Identifier are what a building operator uses to identify your device.
// -----------------------------------------------------------------------------

// Your BACnet Vendor Identifier. 389 = Chipkin Automation Systems; change this
// to YOUR company's vendor ID before shipping a product. Vendor IDs are assigned
// by ASHRAE - request one (free) at https://bacnet.org/assigned-vendor-ids/.
// The virtual devices share the gateway's vendor.
static const uint32_t VENDOR_IDENTIFIER = 389;

// The GATEWAY's Object_Name. Object_Name must be unique across the whole
// BACnet internetwork, and here it is a COMPILE-TIME constant while the
// instance is configurable - ship two units and both announce the same name,
// a spec violation. In a real product make it (and the virtual devices'
// names, built from it below) per-unit configurable.
static const char* DEVICE_NAME = "Chipkin Example B-GW";

static const char* DEVICE_DESCRIPTION_EO =
    "Chipkin CAS BACnet Stack example - B-GW (Gateway), GW-EO-B: the data of three devices "
    "on a SIMULATED foreign bus, as objects in this device.";
static const char* DEVICE_DESCRIPTION_VN =
    "Chipkin CAS BACnet Stack example - B-GW (Gateway), GW-VN-B: three devices on a "
    "SIMULATED foreign bus, as virtual BACnet devices on virtual network 100.";

static const char* VENDOR_NAME = "Chipkin Automation Systems";
static const char* MODEL_NAME = "CAS BACnet Stack Example - B-GW";
static const char* VIRTUAL_MODEL_NAME = "CAS BACnet Stack Example - B-GW (virtual device)";

// DeviceCommunicationControl password ("" = accept any request). Change this
// to your device's secret before shipping.
static const char* DCC_PASSWORD = "";

// Firmware_Revision names the stack's version (filled in main()); the
// Application_Software_Version is APP_VERSION.
static std::string g_firmwareRevision;

// The Network Port object for the gateway's BACnet/IP port.
static const uint32_t NETWORK_PORT_INSTANCE = 1;       // "Vermilion"
static const uint32_t MAX_APDU_LENGTH = 1476;          // BACnet/IP APDU length
static uint8_t g_ipAddress[4] = { 0, 0, 0, 0 };
static uint8_t g_ipSubnetMask[4] = { 0, 0, 0, 0 };
static uint8_t g_ipDefaultGateway[4] = { 0, 0, 0, 0 };
static uint16_t g_bacnetIpUdpPort = 47808;

// Reliability (property 103) and the two values this gateway reports
// (BACnetReliability, cl. 21). The stack builds Status_Flags from it: any
// value other than NO_FAULT_DETECTED sets the FAULT flag.
static const uint32_t PROPERTY_IDENTIFIER_RELIABILITY = 103;
static const uint32_t RELIABILITY_NO_FAULT_DETECTED = 0;
static const uint32_t RELIABILITY_COMMUNICATION_FAILURE = 12;

static const uint32_t BACNET_PRIORITY_ARRAY_SIZE = 16;

// -----------------------------------------------------------------------------
// 2. The gateway's picture of the foreign bus
//
// For every slave the gateway keeps a CACHE - the last value it read, the
// values BACnet clients have commanded - and the state of its conversation
// with that slave. BACnet requests are answered from the cache, never by
// waiting on the foreign bus (Annex H.2.1.4, H.2.3.2).
// -----------------------------------------------------------------------------

// A BACnet commandable value: a 16-slot Priority_Array plus a Relinquish_Default.
// Each slot is either null (relinquished) or holds a commanded value. The
// stack builds Priority_Array, Present_Value and Current_Command_Priority
// from what the callbacks below serve; the gateway resolves the same winner
// itself (WinningValue) to know what to write to the slave.
struct Commandable {
    bool isSet[BACNET_PRIORITY_ARRAY_SIZE];
    double value[BACNET_PRIORITY_ARRAY_SIZE];
    double relinquishDefault;
};

// One output's link to its foreign data point: what was last written there,
// and the write on the wire right now (if any).
struct WriteThrough {
    bool written;              // false = the slave may not hold the winning value yet
    double lastWrittenValue;
    uint32_t transactionId;    // 0 = no write in flight
    double inFlightValue;
    uint64_t retryAt;          // after a failed write, not before this time
};

struct GatewayedSlave {
    uint8_t unitId;                     // the slave's address on the foreign bus
    uint64_t pollPeriodMilliseconds;    // how often the gateway reads its sensor
    uint32_t virtualDeviceInstance;     // --mode vn only

    // The cache.
    double measuredTemperature;         // input register 0, in degrees C
    Commandable setpoint;               // Analog Output -> holding register 0 (degrees C)
    Commandable enable;                 // Binary Output -> coil 0 (0 inactive, 1 active)
    WriteThrough setpointLink;
    WriteThrough enableLink;

    // The conversation.
    bool online;                        // what the gateway BELIEVES (not what is true)
    uint32_t consecutiveFailedPolls;
    uint64_t nextPollAt;
    uint32_t pollTransactionId;         // 0 = no poll in flight
};

// The relinquish defaults match the slaves' starting setpoints and enables,
// so start-up writes leave each slave as it was.
static GatewayedSlave g_slaves[SimulatedForeignBus::SLAVE_COUNT] = {
    // unit, poll ms, virtual device,                     cache: temperature, setpoint, enable
    { 1, 500, FIRST_VIRTUAL_DEVICE_INSTANCE,     0.0, { { false }, { 0 }, 65.0 }, { { false }, { 0 }, 1.0 },
      { false, 0, 0, 0, 0 }, { false, 0, 0, 0, 0 }, true, 0, 0, 0 },
    { 2, 700, FIRST_VIRTUAL_DEVICE_INSTANCE + 1, 0.0, { { false }, { 0 }, 13.0 }, { { false }, { 0 }, 1.0 },
      { false, 0, 0, 0, 0 }, { false, 0, 0, 0, 0 }, true, 0, 0, 0 },
    { 3, 900, FIRST_VIRTUAL_DEVICE_INSTANCE + 2, 0.0, { { false }, { 0 }, 7.0 },  { { false }, { 0 }, 0.0 },
      { false, 0, 0, 0, 0 }, { false, 0, 0, 0, 0 }, true, 0, 0, 0 },
};

// A slave is declared offline after this many polls in a row go unanswered,
// so one lost frame on a noisy bus does not flap the BACnet objects into fault.
static const uint32_t OFFLINE_AFTER_FAILED_POLLS = 2;

// Each mapped point is one of three kinds - one per register-map column.
enum class PointKind { MeasuredTemperature, Setpoint, Enable };

static uint64_t NowMilliseconds() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static uint8_t EffectivePriority(uint8_t priority) {
    return (priority >= 1 && priority <= BACNET_PRIORITY_ARRAY_SIZE) ? priority : 16;
}

static double WinningValue(const Commandable& command) {
    for (uint32_t slotIndex = 0; slotIndex < BACNET_PRIORITY_ARRAY_SIZE; ++slotIndex) {
        if (command.isSet[slotIndex]) {
            return command.value[slotIndex];
        }
    }
    return command.relinquishDefault;
}

// Which mapped point does a BACnet (device, object type, object instance) name?
// --mode eo: the gateway device's object instance n is slave n.
// --mode vn: the virtual device names the slave; every object is instance 1.
static GatewayedSlave* FindMappedPoint(uint32_t deviceInstance, uint16_t objectType,
                                       uint32_t objectInstance, PointKind* kind) {
    PointKind pointKind;
    if (objectType == OBJECT_TYPE_ANALOG_INPUT) {
        pointKind = PointKind::MeasuredTemperature;
    } else if (objectType == OBJECT_TYPE_ANALOG_OUTPUT) {
        pointKind = PointKind::Setpoint;
    } else if (objectType == OBJECT_TYPE_BINARY_OUTPUT) {
        pointKind = PointKind::Enable;
    } else {
        return NULL;
    }
    for (GatewayedSlave& slave : g_slaves) {
        const bool matches = (g_mode == GatewayMode::EmbeddedObjects)
            ? (deviceInstance == g_deviceInstance && objectInstance == slave.unitId)
            : (deviceInstance == slave.virtualDeviceInstance && objectInstance == 1);
        if (matches) {
            if (kind != NULL) {
                *kind = pointKind;
            }
            return &slave;
        }
    }
    return NULL;
}

static GatewayedSlave* FindVirtualDevice(uint32_t deviceInstance) {
    if (g_mode != GatewayMode::VirtualNetwork) {
        return NULL;
    }
    for (GatewayedSlave& slave : g_slaves) {
        if (slave.virtualDeviceInstance == deviceInstance) {
            return &slave;
        }
    }
    return NULL;
}

static Commandable* FindCommandable(uint32_t deviceInstance, uint16_t objectType, uint32_t objectInstance) {
    PointKind kind;
    GatewayedSlave* slave = FindMappedPoint(deviceInstance, objectType, objectInstance, &kind);
    if (slave == NULL) {
        return NULL;
    }
    if (kind == PointKind::Setpoint) {
        return &slave->setpoint;
    }
    if (kind == PointKind::Enable) {
        return &slave->enable;
    }
    return NULL;
}

static bool ReadPrioritySlot(const Commandable* command, uint32_t propertyIdentifier,
                             bool useArrayIndex, uint32_t propertyArrayIndex,
                             bool* slotIsSet, double* slotValue) {
    if (propertyIdentifier != PROPERTY_IDENTIFIER_PRIORITY_ARRAY || !useArrayIndex ||
        propertyArrayIndex < 1 || propertyArrayIndex > BACNET_PRIORITY_ARRAY_SIZE) {
        return false;
    }
    *slotIsSet = command->isSet[propertyArrayIndex - 1];
    *slotValue = command->value[propertyArrayIndex - 1];
    return true;
}

// The objects' names follow docs/colour-table.md: instance 1 is "Bronze",
// instance n is "Bronze n". In --mode vn every object is instance 1.
static std::string MappedObjectName(PointKind kind, uint32_t objectInstance) {
    const char* colour = (kind == PointKind::MeasuredTemperature) ? "Bronze" :
                         (kind == PointKind::Setpoint) ? "Chartreuse" : "Fuchsia";
    if (objectInstance == 1) {
        return colour;
    }
    return std::string(colour) + " " + std::to_string(objectInstance);
}

// Description says where on the foreign bus the object's value lives.
static std::string MappedObjectDescription(const GatewayedSlave& slave, PointKind kind) {
    const char* what = (kind == PointKind::MeasuredTemperature) ? "input register 0 (measured temperature, C x 10)" :
                       (kind == PointKind::Setpoint) ? "holding register 0 (setpoint, C x 10)" :
                       "coil 0 (enable)";
    return std::string("SIMULATED foreign bus: ") + SimulatedForeignBus::SlaveName(slave.unitId) +
           " (unit " + std::to_string(slave.unitId) + ") " + what;
}

static std::string VirtualDeviceName(const GatewayedSlave& slave) {
    return std::string(DEVICE_NAME) + " (" + SimulatedForeignBus::SlaveName(slave.unitId) + ")";
}

static std::string VirtualDeviceDescription(const GatewayedSlave& slave) {
    return std::string("Virtual BACnet device (GW-VN-B) for the ") + SimulatedForeignBus::SlaveName(slave.unitId) +
           " - unit " + std::to_string(slave.unitId) + " on a SIMULATED foreign bus behind Device " +
           std::to_string(g_deviceInstance) + ".";
}

// -----------------------------------------------------------------------------
// 3. Talking to the foreign bus - polling, write-through, offline detection.
// Everything here goes through ForeignBus.h, the seam.
// -----------------------------------------------------------------------------

static void SetVirtualDeviceDiscoverable(const GatewayedSlave& slave, bool discoverable);
static void SendIAmForVirtualDevice(const GatewayedSlave& slave);

static void SlaveWentOffline(GatewayedSlave& slave) {
    slave.online = false;
    // Whatever was written may not have landed; write it again once the slave is back.
    slave.setpointLink.written = false;
    slave.enableLink.written = false;
    printf("GATEWAY: slave %u (%s) stopped answering - its objects now report Reliability "
           "COMMUNICATION_FAILURE + FAULT.\n", slave.unitId, SimulatedForeignBus::SlaveName(slave.unitId));
    if (g_mode == GatewayMode::VirtualNetwork) {
        SetVirtualDeviceDiscoverable(slave, false);
    }
}

static void SlaveCameBack(GatewayedSlave& slave) {
    slave.online = true;
    printf("GATEWAY: slave %u (%s) answers again - fault cleared.\n", slave.unitId,
           SimulatedForeignBus::SlaveName(slave.unitId));
    if (g_mode == GatewayMode::VirtualNetwork) {
        SetVirtualDeviceDiscoverable(slave, true);
        SendIAmForVirtualDevice(slave);
    }
}

static void ServicePoll(GatewayedSlave& slave, uint64_t now) {
    if (slave.pollTransactionId == 0) {
        if (now < slave.nextPollAt) {
            return;
        }
        slave.nextPollAt = now + slave.pollPeriodMilliseconds;
        slave.pollTransactionId = ForeignBus::StartRead(slave.unitId, ForeignBus::Table::InputRegister,
                                                        SimulatedForeignBus::INPUT_REGISTER_MEASURED_TEMPERATURE);
        return;
    }
    uint16_t registerValue = 0;
    const ForeignBus::Outcome outcome = ForeignBus::CheckTransaction(slave.pollTransactionId, &registerValue);
    if (outcome == ForeignBus::Outcome::Pending) {
        return;
    }
    slave.pollTransactionId = 0;
    if (outcome == ForeignBus::Outcome::Done) {
        slave.measuredTemperature = (int16_t)registerValue / 10.0; // C x 10, signed
        slave.consecutiveFailedPolls = 0;
        if (!slave.online) {
            SlaveCameBack(slave);
        }
        return;
    }
    ++slave.consecutiveFailedPolls;
    if (slave.online && slave.consecutiveFailedPolls >= OFFLINE_AFTER_FAILED_POLLS) {
        SlaveWentOffline(slave);
    }
}

// Keep one output's foreign data point equal to the BACnet winning value.
static void ServiceWriteThrough(GatewayedSlave& slave, const Commandable& command, WriteThrough& link,
                                ForeignBus::Table table, uint16_t address, double registerScale, uint64_t now) {
    if (link.transactionId != 0) {
        const ForeignBus::Outcome outcome = ForeignBus::CheckTransaction(link.transactionId, NULL);
        if (outcome == ForeignBus::Outcome::Pending) {
            return;
        }
        link.transactionId = 0;
        if (outcome == ForeignBus::Outcome::Done) {
            link.written = true;
            link.lastWrittenValue = link.inFlightValue;
            printf("GATEWAY: wrote %.1f to slave %u %s %u.\n", link.inFlightValue, slave.unitId,
                   ForeignBus::TableName(table), address);
        }
        if (outcome == ForeignBus::Outcome::Timeout) {
            // written stays false: try again, but not on every pass - a device that keeps
            // refusing a write (a real driver reports a Modbus exception the same way) would
            // otherwise be hammered. One poll cycle apart, and say so on the console.
            link.retryAt = now + slave.pollPeriodMilliseconds;
            printf("GATEWAY: write of %.1f to slave %u %s %u failed - retrying.\n", link.inFlightValue,
                   slave.unitId, ForeignBus::TableName(table), address);
        }
    }
    const double winningValue = WinningValue(command);
    if (!slave.online || now < link.retryAt || (link.written && link.lastWrittenValue == winningValue)) {
        return; // nothing to do - or nobody to tell until the slave is back
    }
    const long scaled = (long)(winningValue * registerScale + (winningValue < 0 ? -0.5 : 0.5));
    link.transactionId = ForeignBus::StartWrite(slave.unitId, table, address, (uint16_t)scaled);
    link.inFlightValue = winningValue;
}

static void ServiceForeignBus() {
    const uint64_t now = NowMilliseconds();
    ForeignBus::Service(now);
    for (GatewayedSlave& slave : g_slaves) {
        ServicePoll(slave, now);
        ServiceWriteThrough(slave, slave.setpoint, slave.setpointLink, ForeignBus::Table::HoldingRegister,
                            SimulatedForeignBus::HOLDING_REGISTER_SETPOINT, 10.0, now);
        ServiceWriteThrough(slave, slave.enable, slave.enableLink, ForeignBus::Table::Coil,
                            SimulatedForeignBus::COIL_ENABLE, 1.0, now);
    }
}

// -----------------------------------------------------------------------------
// 4. Property "get" callbacks. The stack asks; the gateway answers from its
// cache. Every callback dispatches on deviceInstance first: in --mode vn the
// three virtual devices all have an Analog Input 1, and only deviceInstance
// tells them apart. Returning false declines - the stack then answers the
// property itself if it can (see B-ASC's main.cpp for the full essay on the
// errorCode out-parameter and that fallback).
// -----------------------------------------------------------------------------

bool GetPropertyReal(const uint32_t deviceInstance, const uint16_t objectType,
                     const uint32_t objectInstance, const uint32_t propertyIdentifier,
                     float* value, const bool useArrayIndex,
                     const uint32_t propertyArrayIndex, uint32_t* errorCode) {
    (void)errorCode;
    PointKind kind;
    const GatewayedSlave* slave = FindMappedPoint(deviceInstance, objectType, objectInstance, &kind);
    if (slave == NULL) {
        return false;
    }
    if (kind == PointKind::MeasuredTemperature && propertyIdentifier == PROPERTY_IDENTIFIER_PRESENT_VALUE) {
        // The last value read - still served while the slave is offline (H.2.3.2:
        // "shall return values when read and not errors").
        *value = (float)slave->measuredTemperature;
        return true;
    }
    if (kind == PointKind::Setpoint) {
        bool slotIsSet = false;
        double slotValue = 0.0;
        if (ReadPrioritySlot(&slave->setpoint, propertyIdentifier, useArrayIndex, propertyArrayIndex,
                             &slotIsSet, &slotValue)) {
            if (!slotIsSet) {
                return false; // the stack reports a relinquished slot as NULL
            }
            *value = (float)slotValue;
            return true;
        }
        if (propertyIdentifier == PROPERTY_IDENTIFIER_RELINQUISH_DEFAULT) {
            *value = (float)slave->setpoint.relinquishDefault;
            return true;
        }
    }
    return false;
}

bool GetPropertyEnumerated(const uint32_t deviceInstance, const uint16_t objectType,
                           const uint32_t objectInstance, const uint32_t propertyIdentifier,
                           uint32_t* value, const bool useArrayIndex,
                           const uint32_t propertyArrayIndex, uint32_t* errorCode) {
    (void)errorCode;
    PointKind kind;
    const GatewayedSlave* slave = FindMappedPoint(deviceInstance, objectType, objectInstance, &kind);
    if (slave != NULL) {
        if (propertyIdentifier == PROPERTY_IDENTIFIER_RELIABILITY) {
            // Annex H.2.3.2: a communication error with the foreign device is shown
            // in Reliability, and through it (the stack builds Status_Flags from
            // Reliability) in the FAULT flag.
            *value = slave->online ? RELIABILITY_NO_FAULT_DETECTED : RELIABILITY_COMMUNICATION_FAILURE;
            return true;
        }
        if (propertyIdentifier == PROPERTY_IDENTIFIER_UNITS && kind != PointKind::Enable) {
            *value = ENGINEERING_UNITS_DEGREES_CELSIUS;
            return true;
        }
        if (kind == PointKind::Enable) {
            bool slotIsSet = false;
            double slotValue = 0.0;
            if (ReadPrioritySlot(&slave->enable, propertyIdentifier, useArrayIndex, propertyArrayIndex,
                                 &slotIsSet, &slotValue)) {
                if (!slotIsSet) {
                    return false;
                }
                *value = (uint32_t)slotValue;
                return true;
            }
            if (propertyIdentifier == PROPERTY_IDENTIFIER_RELINQUISH_DEFAULT) {
                *value = (uint32_t)slave->enable.relinquishDefault;
                return true;
            }
            if (propertyIdentifier == PROPERTY_IDENTIFIER_POLARITY) {
                *value = POLARITY_NORMAL;
                return true;
            }
        }
        return false;
    }
    if (deviceInstance == g_deviceInstance && objectType == OBJECT_TYPE_NETWORK_PORT &&
        objectInstance == NETWORK_PORT_INSTANCE && propertyIdentifier == PROPERTY_IDENTIFIER_BACNET_IP_MODE) {
        *value = BACNET_IP_MODE_NORMAL;
        return true;
    }
    return false;
}

bool GetPropertyUnsignedInteger(const uint32_t deviceInstance, const uint16_t objectType,
                                const uint32_t objectInstance, const uint32_t propertyIdentifier,
                                uint32_t* value, const bool useArrayIndex,
                                const uint32_t propertyArrayIndex, uint32_t* errorCode) {
    (void)errorCode;
    (void)useArrayIndex;
    (void)propertyArrayIndex;
    // Vendor_Identifier: the gateway and every virtual device.
    if (objectType == OBJECT_TYPE_DEVICE && objectInstance == deviceInstance &&
        propertyIdentifier == PROPERTY_IDENTIFIER_VENDOR_IDENTIFIER &&
        (deviceInstance == g_deviceInstance || FindVirtualDevice(deviceInstance) != NULL)) {
        *value = VENDOR_IDENTIFIER;
        return true;
    }
    if (deviceInstance == g_deviceInstance && objectType == OBJECT_TYPE_NETWORK_PORT &&
        objectInstance == NETWORK_PORT_INSTANCE) {
        if (propertyIdentifier == PROPERTY_IDENTIFIER_APDU_LENGTH) {
            *value = MAX_APDU_LENGTH;
            return true;
        }
        if (propertyIdentifier == PROPERTY_IDENTIFIER_REFERENCE_PORT) {
            *value = NETWORK_PORT_REFERENCE_PORT_NONE;
            return true;
        }
        if (propertyIdentifier == PROPERTY_IDENTIFIER_BACNET_IP_UDP_PORT) {
            *value = g_bacnetIpUdpPort;
            return true;
        }
    }
    return false;
}

bool GetPropertyBool(const uint32_t deviceInstance, const uint16_t objectType,
                     const uint32_t objectInstance, const uint32_t propertyIdentifier,
                     bool* value, const bool useArrayIndex,
                     const uint32_t propertyArrayIndex, uint32_t* errorCode) {
    (void)errorCode;
    const Commandable* command = FindCommandable(deviceInstance, objectType, objectInstance);
    if (command != NULL && propertyIdentifier == PROPERTY_IDENTIFIER_PRIORITY_ARRAY &&
        useArrayIndex && propertyArrayIndex >= 1 && propertyArrayIndex <= BACNET_PRIORITY_ARRAY_SIZE) {
        *value = !command->isSet[propertyArrayIndex - 1]; // true = this slot is NULL
        return true;
    }
    // Out_Of_Service: false for every mapped object and the Network Port.
    if (propertyIdentifier == PROPERTY_IDENTIFIER_OUT_OF_SERVICE &&
        (FindMappedPoint(deviceInstance, objectType, objectInstance, NULL) != NULL ||
         (deviceInstance == g_deviceInstance && objectType == OBJECT_TYPE_NETWORK_PORT))) {
        *value = false;
        return true;
    }
    return false;
}

// OCTET STRING - the gateway's Network Port addressing. The virtual network's
// Network Port (VIRTUAL_NETWORK_PORT_INSTANCE) is populated by the stack.
bool GetPropertyOctetString(const uint32_t deviceInstance, const uint16_t objectType,
                            const uint32_t objectInstance, const uint32_t propertyIdentifier,
                            uint8_t* value, uint32_t* valueElementCount,
                            const uint32_t maxElementCount, const bool useArrayIndex,
                            const uint32_t propertyArrayIndex, uint32_t* errorCode) {
    (void)useArrayIndex;
    (void)propertyArrayIndex;
    (void)errorCode;
    if (deviceInstance != g_deviceInstance || objectType != OBJECT_TYPE_NETWORK_PORT ||
        objectInstance != NETWORK_PORT_INSTANCE || maxElementCount < 4) {
        return false;
    }
    const uint8_t* source = NULL;
    switch (propertyIdentifier) {
        case PROPERTY_IDENTIFIER_IP_ADDRESS:         source = g_ipAddress; break;
        case PROPERTY_IDENTIFIER_IP_SUBNET_MASK:     source = g_ipSubnetMask; break;
        case PROPERTY_IDENTIFIER_IP_DEFAULT_GATEWAY: source = g_ipDefaultGateway; break;
        default: return false;
    }
    memcpy(value, source, 4);
    *valueElementCount = 4;
    return true;
}

static bool ReturnCharacterString(const std::string& text, char* value, uint32_t* valueElementCount,
                                  const uint32_t maxElementCount, uint8_t* encodingType) {
    uint32_t length = (uint32_t)text.size();
    if (length > maxElementCount) {
        length = maxElementCount;
    }
    memcpy(value, text.data(), length);
    *valueElementCount = length;
    *encodingType = CHARACTER_STRING_ENCODING_UTF8;
    return true;
}

bool GetPropertyCharString(const uint32_t deviceInstance, const uint16_t objectType,
                           const uint32_t objectInstance, const uint32_t propertyIdentifier,
                           char* value, uint32_t* valueElementCount,
                           const uint32_t maxElementCount, uint8_t* encodingType,
                           const bool useArrayIndex, const uint32_t propertyArrayIndex,
                           uint32_t* errorCode) {
    (void)useArrayIndex;
    (void)propertyArrayIndex;
    (void)errorCode;

    // --- The mapped objects (both modes) -------------------------------------
    PointKind kind;
    const GatewayedSlave* slave = FindMappedPoint(deviceInstance, objectType, objectInstance, &kind);
    if (slave != NULL) {
        if (propertyIdentifier == PROPERTY_IDENTIFIER_OBJECT_NAME) {
            return ReturnCharacterString(MappedObjectName(kind, objectInstance), value, valueElementCount,
                                         maxElementCount, encodingType);
        }
        if (propertyIdentifier == PROPERTY_IDENTIFIER_DESCRIPTION) {
            return ReturnCharacterString(MappedObjectDescription(*slave, kind), value, valueElementCount,
                                         maxElementCount, encodingType);
        }
        return false;
    }

    // --- Network Port names ------------------------------------------------------
    // The stack creates --mode vn's other Network Ports itself (the gateway's port
    // to virtual network 100, and one inside each virtual device), but it asks
    // this callback for their Object_Name like any other object's - unanswered,
    // they would read "undefined".
    const GatewayedSlave* virtualDevice = FindVirtualDevice(deviceInstance);
    if (objectType == OBJECT_TYPE_NETWORK_PORT && propertyIdentifier == PROPERTY_IDENTIFIER_OBJECT_NAME) {
        if (deviceInstance == g_deviceInstance && objectInstance == NETWORK_PORT_INSTANCE) {
            return ReturnCharacterString("Vermilion", value, valueElementCount, maxElementCount, encodingType);
        }
        if (deviceInstance == g_deviceInstance && g_mode == GatewayMode::VirtualNetwork &&
            objectInstance == VIRTUAL_NETWORK_PORT_INSTANCE) {
            return ReturnCharacterString("Vermilion 2", value, valueElementCount, maxElementCount, encodingType);
        }
        if (virtualDevice != NULL) {
            return ReturnCharacterString("Vermilion", value, valueElementCount, maxElementCount, encodingType);
        }
        return false;
    }

    // --- A virtual device's Device object (--mode vn) ------------------------
    if (virtualDevice != NULL && objectType == OBJECT_TYPE_DEVICE && objectInstance == deviceInstance) {
        switch (propertyIdentifier) {
            case PROPERTY_IDENTIFIER_OBJECT_NAME:
                return ReturnCharacterString(VirtualDeviceName(*virtualDevice), value, valueElementCount, maxElementCount, encodingType);
            case PROPERTY_IDENTIFIER_DESCRIPTION:
                return ReturnCharacterString(VirtualDeviceDescription(*virtualDevice), value, valueElementCount, maxElementCount, encodingType);
            case PROPERTY_IDENTIFIER_VENDOR_NAME:
                return ReturnCharacterString(VENDOR_NAME, value, valueElementCount, maxElementCount, encodingType);
            case PROPERTY_IDENTIFIER_MODEL_NAME:
                return ReturnCharacterString(VIRTUAL_MODEL_NAME, value, valueElementCount, maxElementCount, encodingType);
            case PROPERTY_IDENTIFIER_FIRMWARE_REVISION:
                return ReturnCharacterString(g_firmwareRevision, value, valueElementCount, maxElementCount, encodingType);
            case PROPERTY_IDENTIFIER_APPLICATION_SOFTWARE_VERSION:
                return ReturnCharacterString(APP_VERSION, value, valueElementCount, maxElementCount, encodingType);
            default:
                return false;
        }
    }

    // --- The GATEWAY device -------------------------------------------------
    if (deviceInstance != g_deviceInstance) {
        return false;
    }
    if (objectType == OBJECT_TYPE_DEVICE && objectInstance == g_deviceInstance) {
        switch (propertyIdentifier) {
            case PROPERTY_IDENTIFIER_OBJECT_NAME:
                return ReturnCharacterString(DEVICE_NAME, value, valueElementCount, maxElementCount, encodingType);
            case PROPERTY_IDENTIFIER_DESCRIPTION:
                return ReturnCharacterString(g_mode == GatewayMode::EmbeddedObjects ? DEVICE_DESCRIPTION_EO : DEVICE_DESCRIPTION_VN,
                                             value, valueElementCount, maxElementCount, encodingType);
            case PROPERTY_IDENTIFIER_VENDOR_NAME:
                return ReturnCharacterString(VENDOR_NAME, value, valueElementCount, maxElementCount, encodingType);
            case PROPERTY_IDENTIFIER_MODEL_NAME:
                return ReturnCharacterString(MODEL_NAME, value, valueElementCount, maxElementCount, encodingType);
            case PROPERTY_IDENTIFIER_FIRMWARE_REVISION:
                return ReturnCharacterString(g_firmwareRevision, value, valueElementCount, maxElementCount, encodingType);
            case PROPERTY_IDENTIFIER_APPLICATION_SOFTWARE_VERSION:
                return ReturnCharacterString(APP_VERSION, value, valueElementCount, maxElementCount, encodingType);
            default:
                break;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// 5. Property "set" callbacks - DS-WP-B. A write to an output's Present_Value
// fills one Priority_Array slot (NULL relinquishes it). The callback returns
// at once - the slave gets the new winning value on the next pass of
// ServiceForeignBus() (write caching, Annex H.2.1.1). A write while the slave
// is offline is accepted the same way and lands when it comes back.
// -----------------------------------------------------------------------------

static void CommandWrite(Commandable* command, uint8_t priority, double value) {
    const uint8_t slot = EffectivePriority(priority);
    command->isSet[slot - 1] = true;
    command->value[slot - 1] = value;
}

static const float SETPOINT_MINIMUM_DEGREES = -3276.8f;
static const float SETPOINT_MAXIMUM_DEGREES = 3276.7f;

bool SetPropertyReal(const uint32_t deviceInstance, const uint16_t objectType,
                     const uint32_t objectInstance, const uint32_t propertyIdentifier,
                     const float value, const bool useArrayIndex,
                     const uint32_t propertyArrayIndex, const uint8_t priority,
                     uint32_t* errorCode) {
    (void)useArrayIndex;
    (void)propertyArrayIndex;
    PointKind kind;
    GatewayedSlave* slave = FindMappedPoint(deviceInstance, objectType, objectInstance, &kind);
    if (slave != NULL && kind == PointKind::Setpoint && propertyIdentifier == PROPERTY_IDENTIFIER_PRESENT_VALUE) {
        // The holding register is a signed 16-bit value in tenths of a degree, so only
        // -3276.8 .. 3276.7 can reach the slave. Refuse anything else (and NaN / infinity)
        // here, while the client can still be told, rather than acknowledge a write that
        // would wrap around in the register.
        if (!(value >= SETPOINT_MINIMUM_DEGREES && value <= SETPOINT_MAXIMUM_DEGREES)) {
            *errorCode = ERROR_CODE_VALUE_OUT_OF_RANGE;
            return false;
        }
        CommandWrite(&slave->setpoint, priority, (double)value);
        printf("WriteProperty: Analog Output %u (device %u) <- %.2f @ priority %u -> slave %u holding register 0\n",
               objectInstance, deviceInstance, value, EffectivePriority(priority), slave->unitId);
        return true;
    }
    return false;
}

bool SetPropertyEnumerated(const uint32_t deviceInstance, const uint16_t objectType,
                           const uint32_t objectInstance, const uint32_t propertyIdentifier,
                           const uint32_t value, const bool useArrayIndex,
                           const uint32_t propertyArrayIndex, const uint8_t priority,
                           uint32_t* errorCode) {
    (void)useArrayIndex;
    (void)propertyArrayIndex;
    PointKind kind;
    GatewayedSlave* slave = FindMappedPoint(deviceInstance, objectType, objectInstance, &kind);
    if (slave != NULL && kind == PointKind::Enable && propertyIdentifier == PROPERTY_IDENTIFIER_PRESENT_VALUE) {
        if (value > 1) {
            *errorCode = ERROR_CODE_VALUE_OUT_OF_RANGE;
            return false;
        }
        CommandWrite(&slave->enable, priority, (double)value);
        printf("WriteProperty: Binary Output %u (device %u) <- %s @ priority %u -> slave %u coil 0\n",
               objectInstance, deviceInstance, value ? "active" : "inactive", EffectivePriority(priority),
               slave->unitId);
        return true;
    }
    return false;
}

bool SetPropertyNull(const uint32_t deviceInstance, const uint16_t objectType,
                     const uint32_t objectInstance, const uint32_t propertyIdentifier,
                     const bool useArrayIndex, const uint32_t propertyArrayIndex,
                     const uint8_t priority, uint32_t* errorCode) {
    (void)useArrayIndex;
    (void)propertyArrayIndex;
    (void)errorCode;
    Commandable* command = FindCommandable(deviceInstance, objectType, objectInstance);
    if (command != NULL && propertyIdentifier == PROPERTY_IDENTIFIER_PRESENT_VALUE) {
        command->isSet[EffectivePriority(priority) - 1] = false;
        printf("WriteProperty: relinquished %s %u (device %u) @ priority %u\n",
               objectType == OBJECT_TYPE_ANALOG_OUTPUT ? "Analog Output" : "Binary Output",
               objectInstance, deviceInstance, EffectivePriority(priority));
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// 6. DeviceCommunicationControl callback (DM-DCC-B) - ported from B-ASC's
// main.cpp (see there for why this callback sets *errorCode on every reject).
// Only the GATEWAY answers it.
// -----------------------------------------------------------------------------
bool DeviceCommunicationControl(const uint32_t deviceInstance, const uint8_t enableDisable,
                                const char* password, const uint8_t passwordLength,
                                const bool useTimeDuration, const uint16_t timeDuration,
                                uint32_t* errorCode) {
    if (deviceInstance != g_deviceInstance) {
        *errorCode = ERROR_CODE_OPTIONAL_FUNCTIONALITY_NOT_SUPPORTED;
        return false;
    }
    // Compare the password by LENGTH FIRST, then bytes - a BACnet CharacterString
    // may contain embedded NULs, so strcmp is never safe here.
    const size_t requiredLength = strlen(DCC_PASSWORD);
    if (requiredLength > 0) {
        bool matches = (password != NULL) && (passwordLength == requiredLength);
        for (size_t characterIndex = 0; matches && characterIndex < requiredLength; ++characterIndex) {
            matches = password[characterIndex] == DCC_PASSWORD[characterIndex];
        }
        if (!matches) {
            printf("DeviceCommunicationControl: REJECTED (password failure)\n");
            *errorCode = ERROR_CODE_PASSWORD_FAILURE;
            return false;
        }
    }
    const char* action = (enableDisable == DCC_ENABLE) ? "enable (resume communication)" :
                         (enableDisable == DCC_DISABLE) ? "disable (1) - DEPRECATED, the stack will reject this" :
                         (enableDisable == DCC_DISABLE_INITIATION) ? "disable-initiation (keep responding)" :
                         "unknown";
    if (useTimeDuration) {
        printf("DeviceCommunicationControl: %s for %u minute(s)\n", action, timeDuration);
    } else {
        printf("DeviceCommunicationControl: %s (indefinitely)\n", action);
    }
    return true;
}

// -----------------------------------------------------------------------------
// 7. Announcements and the virtual devices' discoverability (--mode vn)
// -----------------------------------------------------------------------------

// A destination connection string for "the local subnet broadcast of the
// gateway's port" (the same address common/'s SendIAm uses internally).
static void BuildLocalBroadcastConnectionString(uint16_t port, uint8_t connectionString[6]) {
    uint8_t broadcastAddress[4] = { 255, 255, 255, 255 };
    uint8_t localAddress[4], localMask[4];
    if (CASExampleHelper::GetLocalIPv4(localAddress, localMask)) {
        for (int octetIndex = 0; octetIndex < 4; ++octetIndex) {
            broadcastAddress[octetIndex] = (uint8_t)(localAddress[octetIndex] | ~localMask[octetIndex]);
        }
    }
    memcpy(connectionString, broadcastAddress, 4);
    connectionString[4] = (uint8_t)((port >> 8) & 0xFF);
    connectionString[5] = (uint8_t)(port & 0xFF);
}

// --mode vn: the gateway is a router to the virtual network (Annex H.1.1.1),
// so it says so - I-Am-Router-To-Network(100) broadcast on its port (cl. 6.6.2
// has a router broadcast this at start-up). The stack's virtual router also
// answers Who-Is-Router-To-Network for network 100 by itself (NM-RC-B).
static void AnnounceVirtualNetwork() {
    const uint16_t reachableNetworks[] = { VIRTUAL_NETWORK_NUMBER };
    uint8_t connectionString[6];
    BuildLocalBroadcastConnectionString(g_bacnetIpUdpPort, connectionString);
    const bool sent = BACnetStack_SendIAmRouterToNetwork(reachableNetworks, 1, connectionString, 6,
                                                         NETWORK_PORT_INSTANCE, true, 0, NULL, 0);
    printf("FYI: I-Am-Router-To-Network(%u) broadcast: %s\n", (unsigned)VIRTUAL_NETWORK_NUMBER,
           sent ? "sent" : "FAILED");
}

static void SendIAmForVirtualDevice(const GatewayedSlave& slave) {
    CASExampleHelper::SendIAm(slave.virtualDeviceInstance, NETWORK_PORT_INSTANCE);
}

// Annex H.2.2.1: "When a gateway has determined that a non-BACnet device is
// offline, it shall also not send I-Am or I-Have requests on behalf of that
// device." Switching the virtual device's Who-Is and Who-Has off does exactly
// that - the stack stops answering them for that device only. The SAME clause
// also says requests directed to an offline device get no answer at all; the
// stack has no customer export to silence a virtual device's ReadProperty
// (disabling the service would answer it with a Reject - an answer). So reads
// are still answered, from the cache, with Reliability COMMUNICATION_FAILURE.
// That part is a documented gap: README "Known gaps" and TODO.md.
static void SetVirtualDeviceDiscoverable(const GatewayedSlave& slave, bool discoverable) {
    if (!BACnetStack_SetServiceEnabled(slave.virtualDeviceInstance, SERVICE_WHO_IS, discoverable) ||
        !BACnetStack_SetServiceEnabled(slave.virtualDeviceInstance, SERVICE_WHO_HAS, discoverable)) {
        printf("Error: could not %s discovery for virtual device %u.\n", discoverable ? "restore" : "stop",
               slave.virtualDeviceInstance);
    }
}

// -----------------------------------------------------------------------------
// 8. Building the objects
// -----------------------------------------------------------------------------

// One slave's three mapped objects, on deviceInstance (the gateway in --mode
// eo, the slave's virtual device in --mode vn), at objectInstance.
static bool AddMappedObjects(uint32_t deviceInstance, uint32_t objectInstance) {
    const uint16_t objectTypes[] = { OBJECT_TYPE_ANALOG_INPUT, OBJECT_TYPE_ANALOG_OUTPUT, OBJECT_TYPE_BINARY_OUTPUT };
    for (uint16_t objectType : objectTypes) {
        if (!BACnetStack_AddObject(deviceInstance, objectType, objectInstance) ||
            // Reliability is optional on these types; a gateway needs it (H.2.3.2).
            !BACnetStack_SetPropertyEnabled(deviceInstance, objectType, objectInstance,
                                            PROPERTY_IDENTIFIER_RELIABILITY, true) ||
            !BACnetStack_SetPropertyEnabled(deviceInstance, objectType, objectInstance,
                                            PROPERTY_IDENTIFIER_DESCRIPTION, true)) {
            printf("Error: failed to add object type %u instance %u to device %u.\n",
                   objectType, objectInstance, deviceInstance);
            return false;
        }
        if (objectType == OBJECT_TYPE_ANALOG_INPUT) {
            continue;
        }
        // The outputs are commandable: Priority_Array + Relinquish_Default, and a
        // writable Present_Value.
        if (!BACnetStack_SetPropertyEnabled(deviceInstance, objectType, objectInstance,
                                            PROPERTY_IDENTIFIER_PRIORITY_ARRAY, true) ||
            !BACnetStack_SetPropertyEnabled(deviceInstance, objectType, objectInstance,
                                            PROPERTY_IDENTIFIER_RELINQUISH_DEFAULT, true) ||
            !BACnetStack_SetPropertyWritable(deviceInstance, objectType, objectInstance,
                                             PROPERTY_IDENTIFIER_PRESENT_VALUE, true)) {
            printf("Error: failed to make object type %u instance %u on device %u commandable.\n",
                   objectType, objectInstance, deviceInstance);
            return false;
        }
    }
    return true;
}

static bool EnableDeviceServices(uint32_t deviceInstance, bool answersDeviceCommunicationControl) {
    // DS-RP-B, DS-WP-B, DM-DDB-B (Who-Is/I-Am), DM-DOB-B (Who-Has/I-Have).
    const uint32_t services[] = { SERVICE_READ_PROPERTY, SERVICE_WRITE_PROPERTY, SERVICE_WHO_IS,
                                  SERVICE_I_AM, SERVICE_WHO_HAS, SERVICE_I_HAVE };
    for (uint32_t service : services) {
        if (!BACnetStack_SetServiceEnabled(deviceInstance, service, true)) {
            printf("Error: failed to enable service %u on device %u.\n", service, deviceInstance);
            return false;
        }
    }
    // DM-DCC-B.
    if (answersDeviceCommunicationControl &&
        !BACnetStack_SetServiceEnabled(deviceInstance, SERVICE_DEVICE_COMMUNICATION_CONTROL, true)) {
        printf("Error: failed to enable DeviceCommunicationControl.\n");
        return false;
    }
    return true;
}

// --mode eo|vn. Anything else is an error (exit), never a silent default.
static bool ParseModeArg(int argc, char** argv) {
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex) {
        if (strcmp(argv[argumentIndex], "--mode") != 0) {
            continue;
        }
        const char* modeText = (argumentIndex + 1 < argc) ? argv[argumentIndex + 1] : "";
        if (strcmp(modeText, "eo") == 0) {
            g_mode = GatewayMode::EmbeddedObjects;
        } else if (strcmp(modeText, "vn") == 0) {
            g_mode = GatewayMode::VirtualNetwork;
        } else {
            printf("Error: --mode must be 'eo' (GW-EO-B, the default) or 'vn' (GW-VN-B), not '%s'.\n", modeText);
            return false;
        }
    }
    return true;
}

static void PrintExampleCommands() {
    printf("This example also has:\n");
    printf("  --mode eo|vn  eo (default): GW-EO-B, the slaves' data as objects in the gateway.\n");
    printf("                vn: GW-VN-B, each slave a virtual device on virtual network %u.\n",
           (unsigned)VIRTUAL_NETWORK_NUMBER);
    printf("  up / down     nudge the SIMULATED Boiler's sensor by 1.1 C (Analog Input 1 follows)\n");
    printf("  k             kill / revive the SIMULATED Boiler (slave 1) - the Failure Mode\n");
    printf("  r             re-send I-Am-Router-To-Network (vn) or the gateway's I-Am (eo)\n");
}

static void NudgeSimulatedBoiler(double degrees) {
    uint16_t registerValue = 0;
    const uint8_t boiler = SimulatedForeignBus::FIRST_UNIT_ID;
    SimulatedForeignBus::GetPoint(boiler, ForeignBus::Table::InputRegister,
                                  SimulatedForeignBus::INPUT_REGISTER_MEASURED_TEMPERATURE, &registerValue);
    registerValue = (uint16_t)((int16_t)registerValue + (int16_t)(degrees * 10.0));
    SimulatedForeignBus::SetPoint(boiler, ForeignBus::Table::InputRegister,
                                  SimulatedForeignBus::INPUT_REGISTER_MEASURED_TEMPERATURE, registerValue);
    printf("SIMULATION: Boiler sensor = %.1f C (the gateway reads it on its next poll)\n",
           (int16_t)registerValue / 10.0);
}

// -----------------------------------------------------------------------------
// 9. main()
// -----------------------------------------------------------------------------
int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    if (!LoadBACnetFunctions()) {
        fprintf(stderr, "Error: failed to load the CAS BACnet Stack: %s\n",
                CASBACnetStackAdapter_LastError());
        return 1;
    }
    {
        char versionText[32];
        snprintf(versionText, sizeof(versionText), "%u.%u.%u.%u",
                 BACnetStack_GetAPIMajorVersion(), BACnetStack_GetAPIMinorVersion(),
                 BACnetStack_GetAPIPatchVersion(), BACnetStack_GetAPIBuildVersion());
        g_firmwareRevision = versionText;
    }

    if (CASExampleHelper::HandleHelpAndVersionArgs(argc, argv, APP_NAME, APP_VERSION)) {
        for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex) {
            if (strcmp(argv[argumentIndex], "--version") != 0) {
                PrintExampleCommands();
                break;
            }
        }
        return 0;
    }
    if (!ParseModeArg(argc, argv)) {
        return 1;
    }
    const uint16_t port = CASExampleHelper::ParsePortArg(argc, argv, 47808);
    g_deviceInstance = CASExampleHelper::ParseDeviceIdArg(argc, argv, g_deviceInstance);
    if (g_mode == GatewayMode::VirtualNetwork && FindVirtualDevice(g_deviceInstance) != NULL) {
        printf("Error: --deviceID %u is one of the virtual devices' instances (%u-%u); pick another.\n",
               g_deviceInstance, FIRST_VIRTUAL_DEVICE_INSTANCE,
               FIRST_VIRTUAL_DEVICE_INSTANCE + SimulatedForeignBus::SLAVE_COUNT - 1);
        return 1;
    }
    CASExampleHelper::PrintVersion(APP_NAME, APP_VERSION);
    printf("Mode: %s\n", g_mode == GatewayMode::EmbeddedObjects
                             ? "eo - GW-EO-B, the slaves' data as objects in the gateway"
                             : "vn - GW-VN-B, each slave a virtual device on a virtual network");

    // --- Bind the BACnet/IP socket -------------------------------------------
    if (!CASExampleHelper::SetupUDP(port)) {
        return 1;
    }
    g_bacnetIpUdpPort = port;
    if (!CASExampleHelper::GetLocalIPv4(g_ipAddress, g_ipSubnetMask)) {
        printf("FYI: could not read a local IPv4 address; Network Port IP_Address will report 0.0.0.0.\n");
    }

    // --- The foreign bus (SIMULATION in this example) ------------------------
    ForeignBus::Start();
    if (!SimulationControl::Start()) {
        return 1;
    }

    // --- Register callbacks --------------------------------------------------
    CASExampleHelper::SetNetworkPortInstance(NETWORK_PORT_INSTANCE);
    CASExampleHelper::RegisterCommonCallbacks();
    BACnetStack_RegisterCallbackGetPropertyReal(GetPropertyReal);
    BACnetStack_RegisterCallbackGetPropertyEnumerated(GetPropertyEnumerated);
    BACnetStack_RegisterCallbackGetPropertyUnsignedInteger(GetPropertyUnsignedInteger);
    BACnetStack_RegisterCallbackGetPropertyCharacterString(GetPropertyCharString);
    BACnetStack_RegisterCallbackGetPropertyBool(GetPropertyBool);
    BACnetStack_RegisterCallbackGetPropertyOctetString(GetPropertyOctetString);
    BACnetStack_RegisterCallbackSetPropertyReal(SetPropertyReal);
    BACnetStack_RegisterCallbackSetPropertyEnumerated(SetPropertyEnumerated);
    BACnetStack_RegisterCallbackSetPropertyNull(SetPropertyNull);
    BACnetStack_RegisterCallbackDeviceCommunicationControl(DeviceCommunicationControl);

    // --- The GATEWAY device ---------------------------------------------------
    if (!BACnetStack_AddDevice(g_deviceInstance)) {
        printf("Error: Failed to add the Device %u.\n", g_deviceInstance);
        return 1;
    }
    if (!EnableDeviceServices(g_deviceInstance, true)) {
        return 1;
    }
    if (!BACnetStack_AddNetworkPortObject(
            g_deviceInstance, NETWORK_PORT_INSTANCE,
            NETWORK_PORT_NETWORK_TYPE_IPV4,
            NETWORK_PORT_PROTOCOL_LEVEL_BACNET_APPLICATION,
            0,  // networkNumber: not configured
            NETWORK_NUMBER_QUALITY_UNKNOWN,
            NETWORK_PORT_REFERENCE_PORT_NONE)) {
        printf("Error: Failed to add Network Port 1 (Vermilion).\n");
        return 1;
    }
    if (!BACnetStack_SetPropertyEnabled(g_deviceInstance, OBJECT_TYPE_DEVICE,
                                        g_deviceInstance, PROPERTY_IDENTIFIER_DESCRIPTION, true)) {
        printf("Error: Failed to enable Description on the Device object.\n");
        return 1;
    }

    if (g_mode == GatewayMode::EmbeddedObjects) {
        // --- GW-EO-B: every slave's points are objects in the gateway --------
        for (const GatewayedSlave& slave : g_slaves) {
            if (!AddMappedObjects(g_deviceInstance, slave.unitId)) {
                return 1;
            }
        }
    } else {
        // --- GW-VN-B: a virtual network, and a virtual device per slave ------
        // BACnetStack_AddVirtualNetwork turns the gateway into a virtual router
        // and creates the Network Port object for the virtual network.
        if (!BACnetStack_AddVirtualNetwork(g_deviceInstance, VIRTUAL_NETWORK_NUMBER, VIRTUAL_NETWORK_PORT_INSTANCE)) {
            printf("Error: Failed to add virtual network %u (built without STACK_OPTION_VIRTUAL_ROUTER?).\n",
                   (unsigned)VIRTUAL_NETWORK_NUMBER);
            return 1;
        }
        for (const GatewayedSlave& slave : g_slaves) {
            // From here on the virtual device instance is a full device: AddObject,
            // SetServiceEnabled and SetPropertyEnabled all take it directly.
            if (!BACnetStack_AddDeviceToVirtualNetwork(slave.virtualDeviceInstance, VIRTUAL_NETWORK_NUMBER) ||
                !EnableDeviceServices(slave.virtualDeviceInstance, false) ||
                !BACnetStack_SetPropertyEnabled(slave.virtualDeviceInstance, OBJECT_TYPE_DEVICE,
                                                slave.virtualDeviceInstance, PROPERTY_IDENTIFIER_DESCRIPTION, true) ||
                !AddMappedObjects(slave.virtualDeviceInstance, 1)) {
                printf("Error: Failed to set up virtual device %u.\n", slave.virtualDeviceInstance);
                return 1;
            }
        }
    }

    // --- Announce ---------------------------------------------------------------
    CASExampleHelper::SendIAm(g_deviceInstance, NETWORK_PORT_INSTANCE);
    if (g_mode == GatewayMode::VirtualNetwork) {
        AnnounceVirtualNetwork();
        for (const GatewayedSlave& slave : g_slaves) {
            SendIAmForVirtualDevice(slave);
        }
    }

    printf("FYI: Device %u (\"%s\") ready. Vendor ID %u. Gatewaying %u slaves on a SIMULATED "
           "foreign bus (%s). Press 'h' for help.\n",
           g_deviceInstance, DEVICE_NAME, VENDOR_IDENTIFIER, (unsigned)SimulatedForeignBus::SLAVE_COUNT,
           g_mode == GatewayMode::EmbeddedObjects ? "--mode eo: objects in this device"
                                                  : "--mode vn: virtual devices 389119-389121 on network 100");

    // --- Run --------------------------------------------------------------------
    bool running = true;
    while (running) {
        BACnetStack_Tick();
        ServiceForeignBus();
        SimulationControl::Service();

        switch (CASExampleHelper::PollKey()) {
            case CASExampleHelper::KeyCommand::Help:
                CASExampleHelper::PrintHelp(APP_NAME, APP_VERSION);
                PrintExampleCommands();
                break;
            case CASExampleHelper::KeyCommand::Quit:
                running = false;
                break;
            case CASExampleHelper::KeyCommand::ArrowUp:
                NudgeSimulatedBoiler(1.1);
                break;
            case CASExampleHelper::KeyCommand::ArrowDown:
                NudgeSimulatedBoiler(-1.1);
                break;
            case CASExampleHelper::KeyCommand::SimulatedPeerToggle: {
                const uint8_t boiler = SimulatedForeignBus::FIRST_UNIT_ID;
                const bool online = !SimulatedForeignBus::IsSlaveOnline(boiler);
                SimulatedForeignBus::SetSlaveOnline(boiler, online);
                printf("SIMULATION: the Boiler (slave %u) is now %s.\n", boiler,
                       online ? "back ONLINE" : "OFFLINE - watch its objects go to fault");
                break;
            }
            case CASExampleHelper::KeyCommand::RouterAnnounce:
                if (g_mode == GatewayMode::VirtualNetwork) {
                    AnnounceVirtualNetwork();
                } else {
                    CASExampleHelper::SendIAm(g_deviceInstance, NETWORK_PORT_INSTANCE);
                    printf("FYI: --mode eo is not a router; re-sent the gateway's I-Am instead.\n");
                }
                break;
            case CASExampleHelper::KeyCommand::None:
            default:
                break;
        }

#if defined(_WIN32)
        Sleep(1);
#else
        usleep(1000);
#endif
    }

    SimulationControl::Stop();
    CASExampleHelper::RestoreInput();
    CASExampleHelper::ShutdownUDP();
    return 0;
}

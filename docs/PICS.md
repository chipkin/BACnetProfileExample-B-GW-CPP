# BACnet Protocol Implementation Conformance Statement (PICS)

For the **BACnet B-GW (Gateway) C++ example** -
see [README.md](../README.md).

> This is the PICS **for the example as shipped**. It describes a tutorial
> device announcing itself as a Chipkin demo, not a product. When you turn this
> example into your own device, this document is one of the things you rewrite:
> the vendor, model and version rows all come from the
> `CHANGE ALL OF THIS BEFORE YOU SHIP` block at the top of `main.cpp`. The
> example has **not** been submitted for BTL certification.
>
> The example runs as **one of two gateways**, chosen at start-up:
> **`--mode eo`** (the default, GW-EO-B) is one device, 389019 "Chipkin
> Example B-GW", whose objects carry the data of three devices on a foreign
> bus; **`--mode vn`** (GW-VN-B) is that gateway device plus three VIRTUAL
> devices (389119, 389120, 389121) on virtual network 100 behind it, one per
> foreign device. Each mode is a separate claim; where they differ, both are
> stated. **The foreign bus is Simulation** (`simulation/`, see README) - the
> BACnet behaviour described here is real.

## 1. Product description

| | |
|---|---|
| **Vendor Name** | Chipkin Automation Systems |
| **Vendor Identifier** | 389 |
| **Product Name** | CAS BACnet Stack Example - B-GW |
| **Product Model Number** | CAS BACnet Stack Example - B-GW (virtual devices: `CAS BACnet Stack Example - B-GW (virtual device)`) |
| **Application Software Version** | 2.0.0 (`APP_VERSION` in `main.cpp`) |
| **Firmware Revision** | the linked CAS BACnet Stack version (6.0.23.0 at this Stack Pin) |
| **BACnet Protocol Version** | 1 |
| **BACnet Protocol Revision** | 30 |

**Product Description:** a BACnet/IP gateway built on the CAS BACnet Stack.
It polls three devices on a non-BACnet bus (Simulation in this example: a
Modbus-style register map per device) and presents each one's measured
temperature as an Analog Input, its setpoint as a commandable Analog Output
and its enable as a commandable Binary Output - inside the gateway device
(`--mode eo`) or inside one virtual device per foreign device (`--mode vn`).
Reads are answered from the gateway's cache; writes are acknowledged at once
and written through (Annex H.2.1.1). A foreign device that stops answering is
reported through Reliability = `communication-failure` and the FAULT flag
(Annex H.2.3.2), and in `--mode vn` its virtual device stops answering Who-Is
(Annex H.2.2.1).

## 2. BACnet standardized device profile (Annex L)

**B-GW - BACnet Gateway** (Annex L.7.2, Miscellaneous). Annex L.7 lists
GW-EO-B and GW-VN-B for B-GW with the footnote "One of these BIBBs shall be
supported"; each mode of this example is a B-GW through one of them. A
conformant B-GW also satisfies **B-GENERAL** (Annex L.8); that is subsumption,
not a second claim.

## 3. BIBBs supported (Annex K)

| BIBB | Description | `--mode eo` | `--mode vn` |
|---|---|:---:|:---:|
| DS-RP-B | Data Sharing - ReadProperty - B | yes | yes (gateway and virtual devices) |
| DS-WP-B | Data Sharing - WriteProperty - B | yes | yes (the virtual devices' outputs) |
| DM-DDB-B | Device Management - Dynamic Device Binding - B | yes | yes |
| DM-DOB-B | Device Management - Dynamic Object Binding - B | yes | yes |
| DM-DCC-B | Device Management - Device Communication Control - B | yes (gateway) | yes (gateway) |
| GW-EO-B | Gateway - Embedded Objects - B (Annex K.7.2) | **yes** | - |
| GW-VN-B | Gateway - Virtual Network - B (Annex K.7.1) | - | **yes** - see the gap below |
| NM-RC-B | Network Management - Router Configuration - B | - | yes (stack's virtual router) - see the gap below |

Annex K.7.1: a GW-VN-B device "appears to the BACnet network as a router as
described in Annex H" and "shall, at a minimum, support NM-RC-B and DS-RP-B".
In `--mode vn` the stack's virtual router answers Who-Is-Router-To-Network with
I-Am-Router-To-Network(100), and routes requests addressed to DNET 100 to the
virtual devices - **verified on the wire** by `tests/router_test.py`.

**Known gaps (both in the stack; README "Known gaps", TODO.md):**

1. `--mode vn`, Annex H.2.2.1: requests to an OFFLINE foreign device's virtual
   device should get no answer at all. The stack has no customer export to
   silence a virtual device, so they are answered from the cache with
   Reliability = `communication-failure`. (The other half of H.2.2.1 - no I-Am
   or I-Have on its behalf - is met.)
2. The I-Am-Router-To-Network answering Who-Is-Router-To-Network is sent to
   the requester, not with a broadcast MAC address (cl. 6.4.2).

No other BIBBs are supported - in particular not DS-RPM-B, DS-COV-B, any
alarm and event (AE-*) BIBB, scheduling, trending, DM-LM-B, or physical
inter-datalink routing (B-RTR). Annex H.2.3.2's "should support initiation of
event notifications" for faults is not implemented.

## 4. Application services supported

### Gateway (Device 389019), both modes

| Service | Initiate | Execute |
|---|:---:|:---:|
| ReadProperty | no | **yes** |
| WriteProperty | no | **yes** (`--mode eo`: the mapped outputs; `--mode vn`: nothing on the gateway is writable) |
| DeviceCommunicationControl | no | **yes** |
| Who-Is | no | **yes** |
| I-Am | **yes** | - |
| Who-Has | no | **yes** |
| I-Have | **yes** | - |

### Virtual devices (389119-389121), `--mode vn`

| Service | Initiate | Execute |
|---|:---:|:---:|
| ReadProperty | no | **yes** |
| WriteProperty | no | **yes** (Analog Output 1, Binary Output 1) |
| Who-Is | no | **yes** - not while its foreign device is offline |
| I-Am | **yes** | - |
| Who-Has | no | **yes** - not while its foreign device is offline |
| I-Have | **yes** | - |

### Network layer, `--mode vn`

| Message | Initiate | Execute |
|---|:---:|:---:|
| Who-Is-Router-To-Network | no | **yes** (the stack's virtual router) |
| I-Am-Router-To-Network | **yes** (broadcast at start-up and on the `r` key; answer to Who-Is-Router-To-Network) | - |

DeviceCommunicationControl sent to a virtual device is rejected; any service
not listed is rejected. That rejection is part of the profile boundary.

## 5. Segmentation capability

Segmentation is **supported in both directions**, by the stack, on every
device - the gateway and, in `--mode vn`, each virtual device
(`Segmentation_Supported` = `segmented-both`): at `6.x` @ `64b2cfac` the
customer build compiles `STACK_OPTION_SEGMENTATION` in (stack #2992), and this
example keeps the stack's defaults - `Max_Segments_Accepted` 16 and
`APDU_Segment_Timeout` 5000 ms, all read on the wire. Table 12-13 footnote 1:
"These properties are required if, and shall be present only if, segmentation
of any kind is supported" - they are not rows in section 7 because its
generator lists only unconditionally required properties.
`Max_APDU_Length_Accepted` is 1476 octets, the BACnet/IP maximum. (At the
previous pin, `e6de4ffd`, every device answered `no-segmentation`.)

## 6. Standard object types supported

No object is dynamically creatable or deletable. The only writable property
is `Present_Value` of the mapped Analog and Binary Outputs (commandable, the
standard 16-slot `Priority_Array`).

| Object type | Mode | Device | Instance | Object_Name | Optional properties supported |
|---|---|---|:---:|---|---|
| Device | both | gateway | 389019 | Chipkin Example B-GW | Description |
| Network Port | both | gateway | 1 | Vermilion | - |
| Analog Input | eo | gateway | 1, 2, 3 | Bronze, Bronze 2, Bronze 3 | Description, Reliability |
| Analog Output | eo | gateway | 1, 2, 3 | Chartreuse, Chartreuse 2, Chartreuse 3 | Description, Reliability |
| Binary Output | eo | gateway | 1, 2, 3 | Fuchsia, Fuchsia 2, Fuchsia 3 | Description, Reliability |
| Network Port | vn | gateway | 2 | Vermilion 2 | - (virtual network 100, stack-created) |
| Device | vn | virtual | 389119, 389120, 389121 | Chipkin Example B-GW (Boiler / Air Handler / Chiller) | Description |
| Analog Input | vn | each virtual | 1 | Bronze | Description, Reliability |
| Analog Output | vn | each virtual | 1 | Chartreuse | Description, Reliability |
| Binary Output | vn | each virtual | 1 | Fuchsia | Description, Reliability |
| Network Port | vn | each virtual | 1 | Vermilion | - (stack-created) |

The gateway's device instance is configurable at run time with `--deviceID`.
The virtual devices' instances are fixed and do **not** move with it.

## 7. Data link layer options

**BACnet/IP (Annex J)**, UDP port 47808 (0xBAC0) by default, configurable at
run time with `--port`. One socket serves every device.

BBMD is not supported, Foreign Device registration is not supported, and
BACnet/SC, MS/TP, Ethernet and PTP are not supported.

## 8. Device address binding

Static device binding is **not supported**. No device initiates a confirmed
BACnet request, so none needs to bind a peer.

## 9. Networking options

`--mode eo`: not a router.

`--mode vn`: the gateway is a **virtual router** (Annex H.1.1.1):
`BACnetStack_AddVirtualNetwork` gives it virtual network 100 and Network Port
2; each virtual device sits on network 100 with the 3-octet form of its device
instance as its address (389119 = `05 EF FF`). A client reaches it with DNET
100 / DADR, and its answers come back with SNET 100 / SADR. This is not
physical inter-datalink routing (B-RTR). The device is not a BBMD and does not
register as a foreign device.

## 10. Character sets supported

UTF-8 (ANSI X3.4). Supporting a character set does not imply the device can
handle data in all character sets.

## 11. Objects and properties

<!-- OBJECTS-PROPERTIES:BEGIN (generated by tools/gen-objects-properties.py from docs/objects.json - do not edit here) -->
Every object this example creates, and every REQUIRED property of each (per ANSI/ASHRAE 135-2024 clause 12 and the stack's `docs/property-profile-reference.md`), plus the optional properties the example turns on. **Served by** says who answers a ReadProperty: the **stack** generates it, or the **app** serves it from a `GetProperty*` callback in `main.cpp`. A ⚠ row is a required property the app does not serve and the stack would fill with a default - that is a defect, not a feature.

### Device 389019 "Chipkin Example B-GW" - THE GATEWAY device, both modes; vendor 389 (Chipkin Automation Systems); instance configurable with --deviceID. --mode eo: its Object_List is itself, Network Port 1 and the nine mapped objects below (11). --mode vn: itself and Network Ports 1 and 2 (3). The stack rows are device-wide facts only the stack knows - the protocol version and revision it implements, the services and object types it was configured with, the live object list and address-binding table. The accepted rows are the stack's configured defaults for APDU limits, segmentation, system status and database revision; an application that answered them from its own constants could contradict the stack, so this example does not

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| System_Status | BACnetDeviceStatus | stack default, accepted (Generic Enumerated default: `0`) | no |
| Vendor_Name | CharacterString | app | no |
| Vendor_Identifier | Unsigned16 | app | no |
| Model_Name | CharacterString | app | no |
| Firmware_Revision | CharacterString | app | no |
| Application_Software_Version | CharacterString | app | no |
| Description *(optional, enabled)* | CharacterString | app | no |
| Protocol_Version | Unsigned | stack | no |
| Protocol_Revision | Unsigned | stack | no |
| Protocol_Services_Supported | BACnetServicesSupported | stack | no |
| Protocol_Object_Types_Supported | BACnetObjectTypesSupported | stack | no |
| Object_List | BACnetARRAY[N] of BACnetObjectIdentifier | stack | no |
| Max_APDU_Length_Accepted | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_MAX_APDU_LENGTH_ACCEPTED`) | no |
| Segmentation_Supported | BACnetSegmentation | stack default, accepted (`BACnetSegmentation::segmentedBoth` with `STACK_OPTION_SEGME) | no |
| APDU_Timeout | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_APDU_TIMEOUT`) | no |
| Number_Of_APDU_Retries | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_NUMBER_OF_APDU_RETRIES`) | no |
| Device_Address_Binding | BACnetLIST of BACnetAddressBinding | stack | no |
| Database_Revision | Unsigned | stack default, accepted (Generic UnsignedInteger default: `0`) | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Network Port 1 "Vermilion" - the GATEWAY's BACnet/IP port, both modes. Network_Number is NOT configured (0/unknown). Network_Type/Protocol_Level are set from BACnetStack_AddNetworkPortObject()'s arguments at start-up; Changes_Pending is computed and answered natively. Reliability has no fault condition this example detects, so it is accepted at the generic default

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Reliability | BACnetReliability | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | app | no |
| Network_Type | BACnetNetworkType | app | no |
| Protocol_Level | BACnetProtocolLevel | app | no |
| Changes_Pending | Boolean | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Analog Input 1 "Bronze" - --mode eo, on the GATEWAY (389019): instance n is slave unit n, named with the colour and n for n > 1 (one entry stands for instances 1-3). Reads input register 0 of its slave (measured temperature, C x 10). REAL, degrees Celsius; Present_Value is the gateway's cached value from the last poll, still served while the slave is offline. Reliability is no-fault-detected, or communication-failure while the slave is offline - the stack builds Status_Flags (FAULT) from it. Event_State reads normal (no intrinsic alarming)

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | Real | app | no |
| Description *(optional, enabled)* | CharacterString | app | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Reliability *(optional, enabled)* | BACnetReliability | app | no |
| Out_Of_Service | Boolean | app | no |
| Units | BACnetEngineeringUnits | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Analog Output 1 "Chartreuse" - --mode eo, on the GATEWAY (389019): instance n is slave unit n, named with the colour and n for n > 1 (one entry stands for instances 1-3). Writes holding register 0 of its slave (setpoint, C x 10). Commandable via a 16-slot Priority_Array (Annex H.2.1.3); Present_Value/Priority_Array/Current_Command_Priority are resolved by the stack from the slots the app serves through GetPropertyReal/GetPropertyBool. The winning value is written through to the register (write caching, Annex H.2.1.1). Relinquish_Default 65.0 / 13.0 / 7.0 for units 1 / 2 / 3

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | Real | stack | yes |
| Description *(optional, enabled)* | CharacterString | app | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Reliability *(optional, enabled)* | BACnetReliability | app | no |
| Out_Of_Service | Boolean | app | no |
| Units | BACnetEngineeringUnits | app | no |
| Priority_Array | BACnetARRAY[16] of BACnetOptionalReal | stack | no |
| Relinquish_Default | Real | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |
| Current_Command_Priority | BACnetOptionalUnsigned | stack | no |

### Binary Output 1 "Fuchsia" - --mode eo, on the GATEWAY (389019): instance n is slave unit n, named with the colour and n for n > 1 (one entry stands for instances 1-3). Writes coil 0 of its slave (enable). Commandable via a 16-slot Priority_Array; Present_Value/Priority_Array/Current_Command_Priority are resolved by the stack from the slots the app serves through GetPropertyEnumerated/GetPropertyBool; the winning value is written through to the coil. Relinquish_Default active / active / inactive for units 1 / 2 / 3

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | BACnetBinaryPV | stack | yes |
| Description *(optional, enabled)* | CharacterString | app | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Reliability *(optional, enabled)* | BACnetReliability | app | no |
| Out_Of_Service | Boolean | app | no |
| Polarity | BACnetPolarity | app | no |
| Priority_Array | BACnetARRAY[16] of BACnetOptionalBinaryPV | stack | no |
| Relinquish_Default | BACnetBinaryPV | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |
| Current_Command_Priority | BACnetOptionalUnsigned | stack | no |

### Network Port 2 "Vermilion 2" - --mode vn only, on the GATEWAY: the port to virtual network 100, created and populated by BACnetStack_AddVirtualNetwork() (Network_Type virtual, Network_Number 100, quality configured). The app serves only its Object_Name and Out_Of_Service

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Reliability | BACnetReliability | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | app | no |
| Network_Type | BACnetNetworkType | stack default, accepted (Generic Enumerated default: `0`) | no |
| Protocol_Level | BACnetProtocolLevel | stack default, accepted (Generic Enumerated default: `0`) | no |
| Changes_Pending | Boolean | stack default, accepted (Generic Boolean default: `false`) | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Device 389119 "Chipkin Example B-GW (Boiler)" - --mode vn only: one VIRTUAL device per slave on virtual network 100 - 389119 "Chipkin Example B-GW (Boiler)", 389120 "... (Air Handler)", 389121 "... (Chiller)" (one entry stands for the three). Added with BACnetStack_AddDeviceToVirtualNetwork; instances fixed (not moved by --deviceID). Its Object_List is itself, the three mapped objects below and its own stack-created Network Port (5). Reached through the gateway with DNET 100, DADR = the 3-octet instance. While its slave is offline it does not answer Who-Is/Who-Has (Annex H.2.2.1); its reads are still answered (a stack gap - README Known gaps). The stack and accepted rows are as for the gateway

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| System_Status | BACnetDeviceStatus | stack default, accepted (Generic Enumerated default: `0`) | no |
| Vendor_Name | CharacterString | app | no |
| Vendor_Identifier | Unsigned16 | app | no |
| Model_Name | CharacterString | app | no |
| Firmware_Revision | CharacterString | app | no |
| Application_Software_Version | CharacterString | app | no |
| Description *(optional, enabled)* | CharacterString | app | no |
| Protocol_Version | Unsigned | stack | no |
| Protocol_Revision | Unsigned | stack | no |
| Protocol_Services_Supported | BACnetServicesSupported | stack | no |
| Protocol_Object_Types_Supported | BACnetObjectTypesSupported | stack | no |
| Object_List | BACnetARRAY[N] of BACnetObjectIdentifier | stack | no |
| Max_APDU_Length_Accepted | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_MAX_APDU_LENGTH_ACCEPTED`) | no |
| Segmentation_Supported | BACnetSegmentation | stack default, accepted (`BACnetSegmentation::segmentedBoth` with `STACK_OPTION_SEGME) | no |
| APDU_Timeout | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_APDU_TIMEOUT`) | no |
| Number_Of_APDU_Retries | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_NUMBER_OF_APDU_RETRIES`) | no |
| Device_Address_Binding | BACnetLIST of BACnetAddressBinding | stack | no |
| Database_Revision | Unsigned | stack default, accepted (Generic UnsignedInteger default: `0`) | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Analog Input 1 "Bronze" - --mode vn, on EACH virtual device (389119 / 389120 / 389121 for units 1 / 2 / 3), always instance 1 (one entry stands for the three devices). Reads input register 0 of its slave (measured temperature, C x 10). REAL, degrees Celsius; Present_Value is the gateway's cached value from the last poll, still served while the slave is offline. Reliability is no-fault-detected, or communication-failure while the slave is offline - the stack builds Status_Flags (FAULT) from it. Event_State reads normal (no intrinsic alarming)

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | Real | app | no |
| Description *(optional, enabled)* | CharacterString | app | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Reliability *(optional, enabled)* | BACnetReliability | app | no |
| Out_Of_Service | Boolean | app | no |
| Units | BACnetEngineeringUnits | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Analog Output 1 "Chartreuse" - --mode vn, on EACH virtual device (389119 / 389120 / 389121 for units 1 / 2 / 3), always instance 1 (one entry stands for the three devices). Writes holding register 0 of its slave (setpoint, C x 10). Commandable via a 16-slot Priority_Array (Annex H.2.1.3); Present_Value/Priority_Array/Current_Command_Priority are resolved by the stack from the slots the app serves through GetPropertyReal/GetPropertyBool. The winning value is written through to the register (write caching, Annex H.2.1.1). Relinquish_Default 65.0 / 13.0 / 7.0 for units 1 / 2 / 3

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | Real | stack | yes |
| Description *(optional, enabled)* | CharacterString | app | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Reliability *(optional, enabled)* | BACnetReliability | app | no |
| Out_Of_Service | Boolean | app | no |
| Units | BACnetEngineeringUnits | app | no |
| Priority_Array | BACnetARRAY[16] of BACnetOptionalReal | stack | no |
| Relinquish_Default | Real | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |
| Current_Command_Priority | BACnetOptionalUnsigned | stack | no |

### Binary Output 1 "Fuchsia" - --mode vn, on EACH virtual device (389119 / 389120 / 389121 for units 1 / 2 / 3), always instance 1 (one entry stands for the three devices). Writes coil 0 of its slave (enable). Commandable via a 16-slot Priority_Array; Present_Value/Priority_Array/Current_Command_Priority are resolved by the stack from the slots the app serves through GetPropertyEnumerated/GetPropertyBool; the winning value is written through to the coil. Relinquish_Default active / active / inactive for units 1 / 2 / 3

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | BACnetBinaryPV | stack | yes |
| Description *(optional, enabled)* | CharacterString | app | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Reliability *(optional, enabled)* | BACnetReliability | app | no |
| Out_Of_Service | Boolean | app | no |
| Polarity | BACnetPolarity | app | no |
| Priority_Array | BACnetARRAY[16] of BACnetOptionalBinaryPV | stack | no |
| Relinquish_Default | BACnetBinaryPV | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |
| Current_Command_Priority | BACnetOptionalUnsigned | stack | no |

### Network Port 1 "Vermilion" - --mode vn only, inside EACH virtual device: created and populated by BACnetStack_AddDeviceToVirtualNetwork() (Network_Type virtual). The app serves only its Object_Name

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Reliability | BACnetReliability | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | stack default, accepted (Generic Boolean default: `false`) | no |
| Network_Type | BACnetNetworkType | stack default, accepted (Generic Enumerated default: `0`) | no |
| Protocol_Level | BACnetProtocolLevel | stack default, accepted (Generic Enumerated default: `0`) | no |
| Changes_Pending | Boolean | stack default, accepted (Generic Boolean default: `false`) | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

<!-- OBJECTS-PROPERTIES:END -->

## 12. References

- ANSI/ASHRAE Standard 135-2024, Annex A (PICS template), Annex K (BIBBs),
  Annex L (device profiles), Clause 12 (object types).
- [README.md](../README.md) - what this example is and how to build it.
- [TUTORIAL.md](../TUTORIAL.md) - how to extend it, and how to keep this
  document honest when you do.

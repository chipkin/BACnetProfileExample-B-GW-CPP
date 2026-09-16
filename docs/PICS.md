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
> This process is **two BACnet devices**: a real GATEWAY (instance 389019,
> "Rainbow") reachable directly over BACnet/IP, and a VIRTUAL device (instance
> 389119, "Rainbow (virtual)") on virtual network 100 behind it - the device
> the gateway represents. Sections 1-10 below describe the gateway; where the
> virtual device differs, it is called out explicitly. Section 11 lists both
> devices' objects separately.

## 1. Product description

| | |
|---|---|
| **Vendor Name** | Chipkin Automation Systems |
| **Vendor Identifier** | 389 |
| **Product Name** | CAS BACnet Stack Example - B-GW |
| **Product Model Number** | CAS BACnet Stack Example - B-GW |
| **Application Software Version** | 1.0.0 |
| **Firmware Revision** | 1.0.0 |
| **BACnet Protocol Version** | 1 |
| **BACnet Protocol Revision** | 24 |

**Product Description:** a BACnet/IP gateway built on the CAS BACnet Stack. It
presents a real GATEWAY device (three read-only sensor objects, three
commandable output objects, and the one physical Network Port) and represents
ONE VIRTUAL BACnet device (its own Analog Input) on a virtual network behind
it, per the **GW-VN-B** BIBB. Both devices answer ReadProperty for every
required property of every object and are discoverable by Who-Is / I-Am and
Who-Has / I-Have; the gateway also accepts WriteProperty on its commandable
outputs and DeviceCommunicationControl. It is a tutorial for implementers of
the B-GW profile.

The virtual device shares the gateway's vendor identity, application software
version and firmware revision, and answers with its own `Model_Name`
(`CAS BACnet Stack Example - B-GW (virtual device)`) so a client can tell the
two devices apart in a discovery tool even though they share one IP/UDP port.

## 2. BACnet standardized device profile (Annex L)

**B-GW - BACnet Gateway** (Annex L.7, Miscellaneous, combinable with any other
profile family).

This device claims exactly one profile, via the **GW-VN-B** (virtual network)
mechanism rather than GW-EO-B (embedded objects). Because the B-GW
requirements are a superset of B-GENERAL's, a conformant B-GW device also
satisfies **B-GENERAL** (Annex L.8); that is subsumption, not a second claim.

## 3. BIBBs supported (Annex K)

| BIBB | Description |
|---|---|
| DS-RP-B | Data Sharing - ReadProperty - B |
| DS-WP-B | Data Sharing - WriteProperty - B |
| DM-DDB-B | Device Management - Dynamic Device Binding - B |
| DM-DOB-B | Device Management - Dynamic Object Binding - B |
| DM-DCC-B | Device Management - Device Communication Control - B |
| GW-VN-B | Gateway - Virtual Network - B |

No other BIBBs are supported. In particular this device does **not** support
DS-RPM-B (ReadPropertyMultiple), DS-COV-B, any alarm and event (AE-*) BIBB,
scheduling (SCHED-*), trending (T-*), or NM-RC-B / DM-LM-B (physical
inter-datalink routing - that is B-RTR's F-ROUTER/F-MULTIPORT, a different
profile).

**DS-WP-B, DM-DCC-B and GW-VN-B apply to the GATEWAY only.** The virtual
device answers DS-RP-B, DM-DDB-B and DM-DOB-B on itself, but has no writable
property and does not answer DeviceCommunicationControl.

## 4. Application services supported

### Gateway (Device 389019)

| Service | Initiate | Execute |
|---|:---:|:---:|
| ReadProperty | no | **yes** |
| WriteProperty | no | **yes** |
| DeviceCommunicationControl | no | **yes** |
| Who-Is | no | **yes** |
| I-Am | **yes** | - |
| Who-Has | no | **yes** |
| I-Have | **yes** | - |

### Virtual device (Device 389119)

| Service | Initiate | Execute |
|---|:---:|:---:|
| ReadProperty | no | **yes** |
| Who-Is | no | **yes** |
| I-Am | **yes** | - |
| Who-Has | no | **yes** |
| I-Have | **yes** | - |

An unsolicited I-Am is broadcast for BOTH devices at start-up, and again on
the `r` key, both from the gateway's one Network Port (the virtual device has
no socket of its own).

Any other confirmed service on either device - including WriteProperty or
DeviceCommunicationControl sent to the virtual device - is rejected. That
rejection is part of the profile boundary, not a limitation to work around.

## 5. Segmentation capability

Segmentation is **not supported** in either direction, on either device
(`Segmentation_Supported` = `no-segmentation`). `Max_APDU_Length_Accepted` is
1476 octets, the BACnet/IP maximum.

## 6. Standard object types supported

No object is dynamically creatable or deletable. On the gateway, only the
three commandable outputs' `Present_Value` is writable (via the standard
16-slot `Priority_Array`); every other property, on both devices, is
read-only.

| Object type | Device | Instance | Object_Name | Optional properties supported |
|---|---|:---:|---|---|
| Device | gateway | 389019 | Rainbow | Description |
| Analog Input | gateway | 1 | Bronze | - |
| Binary Input | gateway | 1 | Emerald | - |
| Multi-State Input | gateway | 1 | Hot Pink | State_Text |
| Analog Output | gateway | 1 | Chartreuse | - |
| Binary Output | gateway | 1 | Fuchsia | - |
| Multi-State Output | gateway | 1 | Indigo | - |
| Network Port | gateway | 1 | Vermilion | - |
| Network Port | gateway | 2 | (unnamed) | - |
| Device | virtual (389119) | 389119 | Rainbow (virtual) | Description |
| Analog Input | virtual (389119) | 1 | Bronze | - |
| Network Port | virtual (389119) | 1 | (unnamed) | - |

The gateway's device instance is configurable at run time with `--deviceID`
(BACnet requires the device instance to be configurable). The virtual
device's instance (`389119`) is fixed at the gateway's default instance + 100
and does **not** move with `--deviceID`.

## 7. Data link layer options

**BACnet/IP (Annex J)**, UDP port 47808 (0xBAC0) by default, configurable at
run time with `--port`. One socket serves both devices.

BBMD is not supported, Foreign Device registration is not supported, and
BACnet/SC, MS/TP, Ethernet (Annex H) and PTP are not supported.

## 8. Device address binding

Static device binding is **not supported**. Neither device initiates a
confirmed request, so neither needs to bind a peer.

## 9. Networking options

The gateway is a **virtual router**: `BACnetStack_AddVirtualNetwork` gives it
a second, virtual Network Port (instance 2) and virtual network 100, behind
which `BACnetStack_AddDeviceToVirtualNetwork` places the virtual device. A
routed request to the virtual device carries DNET 100 and the virtual
device's MAC address on that network, which the stack derives from the device
instance itself.

This is **not** physical inter-datalink routing (that is B-RTR's F-ROUTER/
F-MULTIPORT profile, a different stack code path with a different
limitation); the virtual network has no second physical datalink to
instantiate. The device is not a BBMD and does not register as a foreign
device.

## 10. Character sets supported

UTF-8 (ANSI X3.4). Supporting a character set does not imply the device can
handle data in all character sets.

## 11. Objects and properties

<!-- OBJECTS-PROPERTIES:BEGIN (generated by tools/gen-objects-properties.py from docs/objects.json - do not edit here) -->
Every object this example creates, and every REQUIRED property of each (per ANSI/ASHRAE 135-2024 clause 12 and the stack's `docs/property-profile-reference.md`), plus the optional properties the example turns on. **Served by** says who answers a ReadProperty: the **stack** generates it, or the **app** serves it from a `GetProperty*` callback in `main.cpp`. A ⚠ row is a required property the app does not serve and the stack would fill with a default - that is a defect, not a feature.

### Device 389019 "Rainbow" - THE GATEWAY device; vendor 389 (Chipkin Automation Systems); instance configurable with --deviceID. The stack rows are device-wide facts only the stack knows - the protocol version and revision it implements, the services and object types it was configured with (from this example's own BACnetStack_SetServiceEnabled/AddObject calls), the live object list and address-binding table. The accepted rows are the stack's configured defaults for APDU limits, segmentation, system status and database revision; an application that answered them from its own constants could contradict the stack, so this example does not

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
| Segmentation_Supported | BACnetSegmentation | stack default, accepted (`BACnetSegmentation::noSegmentation`) | no |
| APDU_Timeout | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_APDU_TIMEOUT`) | no |
| Number_Of_APDU_Retries | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_NUMBER_OF_APDU_RETRIES`) | no |
| Device_Address_Binding | BACnetLIST of BACnetAddressBinding | stack | no |
| Database_Revision | Unsigned | stack default, accepted (Generic UnsignedInteger default: `0`) | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Analog Input 1 "Bronze" - belongs to the GATEWAY device (389019). REAL, degrees Celsius; starts at 21.5; read-only; nudged by the up/down keys

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | Real | app | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | app | no |
| Units | BACnetEngineeringUnits | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Binary Input 1 "Emerald" - belongs to the GATEWAY device (389019). starts inactive; read-only

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | BACnetBinaryPV | app | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | app | no |
| Polarity | BACnetPolarity | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Multi-state Input 1 "Hot Pink" - belongs to the GATEWAY device (389019). state 1 of 3: On, Off, Auto; read-only

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | Unsigned | app | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | app | no |
| Number_Of_States | Unsigned | app | no |
| State_Text *(optional, enabled)* | BACnetARRAY[N] of CharacterString | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Analog Output 1 "Chartreuse" - belongs to the GATEWAY device (389019). REAL setpoint, default 20.0 C; commandable via a 16-slot Priority_Array (WriteProperty, DS-WP-B) - Present_Value/Priority_Array/Current_Command_Priority are resolved by the stack from the slots the app serves through GetPropertyReal/GetPropertyBool

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | Real | stack | yes |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | app | no |
| Units | BACnetEngineeringUnits | app | no |
| Priority_Array | BACnetARRAY[16] of BACnetOptionalReal | stack | no |
| Relinquish_Default | Real | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |
| Current_Command_Priority | BACnetOptionalUnsigned | stack | no |

### Binary Output 1 "Fuchsia" - belongs to the GATEWAY device (389019). active/inactive, default inactive; commandable via a 16-slot Priority_Array (WriteProperty, DS-WP-B) - Present_Value/Priority_Array/Current_Command_Priority are resolved by the stack from the slots the app serves through GetPropertyEnumerated/GetPropertyBool

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | BACnetBinaryPV | stack | yes |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | app | no |
| Polarity | BACnetPolarity | app | no |
| Priority_Array | BACnetARRAY[16] of BACnetOptionalBinaryPV | stack | no |
| Relinquish_Default | BACnetBinaryPV | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |
| Current_Command_Priority | BACnetOptionalUnsigned | stack | no |

### Multi-state Output 1 "Indigo" - belongs to the GATEWAY device (389019). state 1 of 3, default state 1; commandable via a 16-slot Priority_Array (WriteProperty, DS-WP-B) - Present_Value/Priority_Array/Current_Command_Priority are resolved by the stack from the slots the app serves through GetPropertyUnsignedInteger/GetPropertyBool

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | Unsigned | stack | yes |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | app | no |
| Number_Of_States | Unsigned | app | no |
| Priority_Array | BACnetARRAY[16] of BACnetOptionalUnsigned | stack | no |
| Relinquish_Default | Unsigned | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |
| Current_Command_Priority | BACnetOptionalUnsigned | stack | no |

### Network Port 1 "Vermilion" - belongs to the GATEWAY device (389019). BACnet/IP, the device's one physical port - Network_Number is NOT configured here (0/unknown), unlike B-RTR's ports. Network_Type/Protocol_Level are set from BACnetStack_AddNetworkPortObject()'s arguments at start-up. Changes_Pending is likewise computed and answered natively. Reliability has no fault condition this example detects, so it is accepted at the generic default (normal).

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

### Network Port 2 "(unnamed)" - belongs to the GATEWAY device (389019). Created internally and fully populated by BACnetStack_AddVirtualNetwork() for the virtual network (100) - this application registers no callback for it and never calls SetPropertyEnabled/SetPropertyWritable on it. Verified on the wire: Object_Name reads back the stack's generic default 'undefined' (no GetPropertyCharacterString case matches it); Network_Type reads back 'virtual' (7), confirming the stack itself created and typed this port correctly.

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | stack default, accepted (the literal string `"undefined"` (non-Device objects only)) | no |
| Object_Type | BACnetObjectType | stack | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Reliability | BACnetReliability | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | stack default, accepted (Generic Boolean default: `false`) | no |
| Network_Type | BACnetNetworkType | stack default, accepted (Generic Enumerated default: `0`) | no |
| Protocol_Level | BACnetProtocolLevel | stack default, accepted (Generic Enumerated default: `0`) | no |
| Changes_Pending | Boolean | stack default, accepted (Generic Boolean default: `false`) | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Device 389119 "Rainbow (virtual)" - THE VIRTUAL device (GW-VN-B) - the thing this gateway represents, NOT the gateway device above. Added with BACnetStack_AddDeviceToVirtualNetwork after BACnetStack_AddVirtualNetwork(389019, network 100, ...); instance is the gateway's instance + 100, fixed (not moved by --deviceID). It is a SEPARATE device with its own Object_List, reached through the gateway's one physical Network Port (it has no socket of its own) - its own Network Port object (instance 1, stack-created) is listed separately below. The stack and accepted rows are exactly as for the gateway above, since this device is set up with the same calls.

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
| Segmentation_Supported | BACnetSegmentation | stack default, accepted (`BACnetSegmentation::noSegmentation`) | no |
| APDU_Timeout | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_APDU_TIMEOUT`) | no |
| Number_Of_APDU_Retries | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_NUMBER_OF_APDU_RETRIES`) | no |
| Device_Address_Binding | BACnetLIST of BACnetAddressBinding | stack | no |
| Database_Revision | Unsigned | stack default, accepted (Generic UnsignedInteger default: `0`) | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Analog Input 1 "Bronze" - belongs to the VIRTUAL device (389119), NOT the gateway's Analog Input 1 above - same object type/instance/name by series convention, but a completely separate object under a separate device, proving GW-VN-B: the represented device has something to actually be read. REAL, degrees Celsius; starts at 19.0; read-only; independent of the gateway's own Analog Input 1

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | app | no |
| Object_Type | BACnetObjectType | stack | no |
| Present_Value | Real | app | no |
| Status_Flags | BACnetStatusFlags | stack | no |
| Event_State | BACnetEventState | stack default, accepted (Generic Enumerated default: `0`) | no |
| Out_Of_Service | Boolean | app | no |
| Units | BACnetEngineeringUnits | app | no |
| Property_List | BACnetARRAY[N] of BACnetPropertyIdentifier | stack | no |

### Network Port 1 "(unnamed)" - belongs to the VIRTUAL device (389119). Created internally and fully populated by BACnetStack_AddDeviceToVirtualNetwork() - not the gateway's own Network Port 2 above, a separate object under the virtual device. This application registers no callback for it. Verified on the wire: Object_Name reads back the stack's generic default 'undefined'; Network_Type reads back 'virtual' (7).

| Property | Datatype | Served by | Writable |
|---|---|---|:---:|
| Object_Identifier | BACnetObjectIdentifier | stack | no |
| Object_Name | CharacterString | stack default, accepted (the literal string `"undefined"` (non-Device objects only)) | no |
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

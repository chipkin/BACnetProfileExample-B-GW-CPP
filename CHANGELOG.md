# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - unreleased

### Added

- First release: implements the **B-GW (Gateway)** profile - a device that
  represents ONE non-BACnet (or otherwise not-directly-reachable) device as a
  virtual BACnet device on a virtual network behind it.
- BIBBs: DS-RP-B, DS-WP-B, DM-DDB-B, DM-DOB-B, DM-DCC-B, GW-VN-B.
- Objects on the GATEWAY (Device 389019 "Rainbow"): Analog/Binary/Multi-State
  Input 1 ("Bronze"/"Emerald"/"Hot Pink"); commandable Analog/Binary/Multi-State
  Output 1 ("Chartreuse"/"Fuchsia"/"Indigo"); Network Port 1 ("Vermilion").
- Objects on the VIRTUAL device (Device 389119 "Rainbow (virtual)", on virtual
  network 100): its own, independent Analog Input 1 ("Bronze").
- Seeded from `BACnetProfileExample-B-RTR-CPP`, with the second physical
  Network Port and physical inter-datalink routing (F-ROUTER/F-MULTIPORT, not
  part of this profile) removed, and `DeviceCommunicationControl` (DM-DCC-B,
  ported from `BACnetProfileExample-B-ASC-CPP`) added back.
- **F-GATEWAY** (series-canonical): `BACnetStack_AddVirtualNetwork` (creates
  the virtual network and its Network Port object) then
  `BACnetStack_AddDeviceToVirtualNetwork` (adds the represented virtual
  device); the virtual device's own objects/services are then configured with
  ordinary `BACnetStack_AddObject`/`SetServiceEnabled`/`SetPropertyEnabled`
  calls naming its device instance, exactly like a second, independent
  example process - except it shares the gateway's one UDP socket and
  Network Port.
- Reuses the `r` key (`common/` 2.4.0+, claimed by B-RTR for "routing/gateway
  examples") to re-send I-Am for both devices on demand; this example's `r`
  handler sends plain I-Am rather than I-Am-Router-To-Network, since a
  gateway is not a router and sends no network-layer router messages.
- Pinned to CAS BACnet Stack `6.x` @ `abd4cee1` (reports 6.0.21), linked as a
  prebuilt STATIC library (`tools/build-stack-static.sh`).
- Vendors `common/` 2.5.0 verbatim (no `common/` change needed for this repo).

### Verified

- STATIC build, zero warnings from `main.cpp`/`common/`.
- `BACnetStack_AddVirtualNetwork`/`BACnetStack_AddDeviceToVirtualNetwork` are
  compiled into the customer-facing `ReleaseLib|x64` static library at this
  pin (`STACK_OPTION_GW_VN_B` -> `STACK_OPTION_VIRTUAL_ROUTER`, both inside
  `STACK_OPTION_ENABLE_ALL_BIBBS`, which `STACK_OPTION_TARGET_FULL` pulls in -
  confirmed by reading `CASBACnetStackOptions.h`/`CIBuildSettings.h` at the
  pin, not assumed). Unlike B-RTR's *physical* two-port routing, virtual
  routing does not depend on the stack's single-instance-per-datalink-type
  limitation (stack issue #2037) - see the README's Verification section for
  what was confirmed live, not merely read from source.
- Live-client wire verification: see README "Verification" for the exact
  commands and observed results.

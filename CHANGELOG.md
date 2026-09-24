# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.2] - unreleased

### Changed

- **Device renamed from the series' colour placeholder "Rainbow" to "Chipkin
  Example B-GW"** so devices from different examples in the series are
  distinguishable from each other on the same BACnet network - every example
  previously announced the identical Object_Name "Rainbow", which made two
  examples on one subnet indistinguishable by name. Sub-object names (Analog
  Input 1 "Bronze", etc.) are unchanged - only the Device object's name
  changed. `docs/colour-table.md` (series root) updated to match. APP_VERSION
  bumped 1.0.1 -> 1.0.2.

## [Unreleased]

### Fixed

- **`Application_Software_Version` (12) and `Firmware_Revision` (44) were
  hardcoded and stale on both devices (the gateway, Device 389019, and the
  virtual device it represents, Device 389119)** - both reported the literal
  `"1.0.0"` via separate `APPLICATION_SOFTWARE_VERSION`/`FIRMWARE_REVISION`
  constants nobody updated across releases, same pattern already fixed in
  B-SCHUB-CPP. Fixed: `Application_Software_Version` now reads `APP_VERSION`
  directly (one source of truth, can't drift from `--version`'s own banner
  again). `Firmware_Revision` is now built at runtime from the CAS BACnet
  Stack's own `BACnetStack_GetAPIMajorVersion()`/`GetAPIMinorVersion()`/
  `GetAPIPatchVersion()`/`GetAPIBuildVersion()` (the same 4 calls
  `common/CASExampleHelper.cpp`'s `PrintVersion()` already uses for the
  startup banner), populated once right after `LoadBACnetFunctions()`
  succeeds, into a new `g_firmwareRevision`. The old separate constants are
  removed entirely. Verified with a real ReadProperty against the running
  device (Device 389019): `Application_Software_Version = "1.0.1"`,
  `Firmware_Revision = "6.0.21.0"` - both now match the actual running build
  instead of a stale hardcoded string. (The virtual device, 389119, shares
  the same fixed code path but was not separately confirmed over the
  network - it sits behind the gateway's virtual network and a plain
  unicast ReadProperty to it returned `unknown-object`, which needs
  BACnet routing/NPDU addressing to reach, not a code issue.)

### Changed

- Restructured the documentation to match the series' README/TUTORIAL/PICS
  split: `README.md` is now scoped to this example only (series framing,
  "What the profile requires" prose, "Before you ship", "Get the code",
  "Link mode", "Troubleshooting", and "Objects and properties" removed or
  moved out - see below), 748 lines down to under 460.
- Added `TUTORIAL.md`: extending the example (including a new
  add-a-commandable-output recipe and a dedicated write-up of the
  dispatch-on-`deviceInstance`-first pitfall unique to a two-device gateway),
  what each object type needs served, a "who serves what" breakdown for the
  representative commandable Analog Output, reviewing your device, and
  Troubleshooting (carried over verbatim from the old README).
- Added `docs/PICS.md`: a full ANSI/ASHRAE 135 Annex A conformance statement
  covering BOTH devices, with the generated objects-and-properties tables
  moved out of `README.md`.
- `docs/objects.json`: split the stack-computed "stack" properties
  (`Object_List`, `Protocol_Version`, `Protocol_Revision`,
  `Protocol_Services_Supported`, `Protocol_Object_Types_Supported`,
  `Device_Address_Binding`) out of "accepted" for both Device entries
  (389019 and 389119), matching the rest of the series; regenerating now
  produces zero ⚠ rows.
- `main.cpp`: absorbed the README's "Before you ship" table into per-constant
  comments in the `CHANGE ALL OF THIS BEFORE YOU SHIP` block, including the
  `Object_Name` uniqueness warning on both `DEVICE_NAME` and
  `VIRTUAL_DEVICE_NAME`.
- **Build switched from a prebuilt STATIC library to the adapter's default
  SOURCE mode**, matching the rest of the series: `cmake -B build -S .` /
  `cmake --build build --config Release` with no link-mode flag and no
  `tools/build-stack-static.sh` step. `CMakeLists.txt`,
  `.github/workflows/release.yml` (link-mode assertion, metrics
  `"link_mode"`, matrix `lib:` entries and the static-library cache/build
  steps removed; `TUTORIAL.md`/`docs/PICS.md` added to the packaged
  artifact) and `AGENTS.md` updated to match. The Footprint table's numbers
  are still from the v1.0.0 STATIC-linked release; the next release
  refreshes them under the SOURCE build.

## [1.0.0] - 2026-09-15

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

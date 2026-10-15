# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - unreleased

The example now meets the series' Profile-functional bar: it performs a
gateway's Profile Function against simulated foreign devices, shows its
Failure Mode, and proves both with Wire Tests. Major version: the BACnet
Surface changed (objects removed and renumbered, new devices in `--mode vn`).

### Added

- **A simulated foreign bus behind one seam.** `ForeignBus.h` is the
  asynchronous interface between the gateway and the non-BACnet network
  (`StartRead` / `StartWrite` / `CheckTransaction`); `simulation/
  SimulatedForeignBus.cpp` implements it with three in-memory slaves (Boiler,
  Air Handler, Chiller), each with a Modbus-style register map (input register,
  holding register, coil), its own poll cycle (500/700/900 ms) and latency
  (20/45/80 ms), and a drifting sensor. README says plainly it is Simulation and
  that a real product implements `ForeignBus.h` with its own driver (Modbus
  TCP/RTU, a PLC protocol, ...).
- **`--mode eo` (default, GW-EO-B, Annex K.7.2)**: each slave's input register
  is an Analog Input that follows the polled value, its holding register a
  commandable Analog Output and its coil a commandable Binary Output, on the
  gateway. Full 16-slot Priority_Array (Annex H.2.1.3); the winning value is
  written through asynchronously after the Result(+) (write caching, H.2.1.1)
  and re-sent after an outage. A slave that misses two polls: Reliability =
  `communication-failure` and the FAULT flag on its objects, values still read
  (H.2.3.2); recovery when it answers again.
- **`--mode vn` (GW-VN-B, Annex K.7.1)**: each slave is a virtual device
  (389119-389121) on virtual network 100 with the same three objects. The
  gateway broadcasts I-Am-Router-To-Network(100) at start-up and on `r`; the
  stack's virtual router answers Who-Is-Router-To-Network (NM-RC-B), now
  verified on the wire. An offline slave's virtual device stops answering
  Who-Is / Who-Has (no I-Am / I-Have on its behalf, H.2.2.1).
- **Wire Tests** (`tests/`, both modes, Python-simulated client, the simulated
  bus driven through a test-only control socket enabled by the
  `BGW_SIMULATION_CONTROL_PORT` environment variable - not a CLI option):
  mapping (Surface, input tracking, write-through, priorities, relinquish),
  Failure Mode (fault, recovery, cached write delivered, vn I-Am suppression),
  router (NM-RC-B, routed reads, eo control). Each verified to fail with its
  feature removed (record in `tests/README.md`). Local run 2026-10-14 (WSL
  Ubuntu 22.04, STATIC build of stack `e6de4ffd`): eo 24/24, 15/15, 2/2; vn
  27/27, 19/19, 10/10 - ALL WIRE TESTS PASSED, two known gaps reported.
- CI: a Linux "Wire Tests (both modes)" step after the smoke test.
- `wiretest/` 0.3.0 vendored (B-RTR's 0.2.0 plus generic additions: see
  `wiretest/CHANGELOG.md`). `common/` 3.2.0 from BACnetProfileExample-B-SS-CPP
  (3.1.0: the `k` key, `KeyCommand::SimulatedPeerToggle`; 3.2.0: a frame the stack
  hands over with no address is sent as a broadcast).
- WriteProperty of an Analog Output value the 16-bit register cannot hold
  (outside -3276.8 .. 3276.7, or NaN / infinity) is refused with
  VALUE_OUT_OF_RANGE instead of acknowledged and wrapped. A write the slave
  does not acknowledge is retried one poll cycle later, not on every pass.
- `--mode vn` refuses a `--deviceID` that collides with a virtual device.
- The stack-created Network Ports are named ("Vermilion 2" on the gateway,
  "Vermilion" in each virtual device) instead of reading `"undefined"`.
- `TODO.md`: the two known gaps, both in the stack - an offline virtual device
  cannot be made silent to requests (H.2.2.1; no customer export), and
  I-Am-Router-To-Network answering Who-Is-Router-To-Network is unicast
  (cl. 6.4.2).

### Removed

- The B-SS template objects that mapped to nothing: Binary Input 1
  "Emerald", Multi-State Input 1 "Hot Pink", Multi-State Output 1 "Indigo".
  Analog Input 1, Analog Output 1 and Binary Output 1 remain, now mapped to
  slave 1 (instances 2 and 3 to slaves 2 and 3).
- The single constant virtual device 389119 "Chipkin Example B-GW (virtual)"
  (Analog Input 19.0, always). `--mode vn` replaces it with one live virtual
  device per slave.

### Changed

- up/down now nudge the simulated Boiler's sensor (Analog Input 1 follows on
  the next poll) instead of setting the Analog Input directly.
- README (scenario, register map, Simulation note, known gaps), TUTORIAL (the
  seam, adding a point, offline handling), PICS (both modes), AGENTS.md.

## [1.0.2] - unreleased

### Changed - honesty pass (2026-10-14)

- **Stack Pin moved `abd4cee1` (6.0.21) -> `6.x` @ `e6de4ffd` (6.0.23)**, per
  docs/adr/0001 (the pin follows the latest `6.x` when an example is updated).
  Not the `6.x` tip `4b27bd56d`: that tip fails to link any customer
  (non-test-tool) build - `RecordTypedGetDecline` is defined only under
  `BACNET_STACK_TESTTOOL` (stack #3850, fixed on `issues/runbook` but not yet
  promoted to `6.x`). `e6de4ffd` is the newest `6.x` commit that links. Builds
  clean (STATIC) with no source change.
- **Protocol_Revision is 30, not 24** - the stack default at this pin. README,
  PICS, TUTORIAL, AGENTS.md and `main.cpp` comments said 24.
- README version banner and sample output, and PICS Application Software
  Version / Firmware Revision, now match what the program reports
  (`APP_VERSION`, the linked stack version, `common/` 3.0.0).
- Series profile table re-synced (B-ACCR and B-DAP marked as Blocked Examples).
- **NM-RC-B is no longer denied.** README, PICS, AGENTS.md and `main.cpp` said
  the example does not implement NM-RC-B; Annex K.7.1 makes NM-RC-B mandatory
  for GW-VN-B. The stack's virtual router answers Who-Is-Router-To-Network for
  the virtual network (read from the stack source, not wire-verified), so
  NM-RC-B and GW-VN-B are now marked unverified.
- README says plainly that the virtual device's value is a constant and no
  foreign protocol is simulated yet.

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

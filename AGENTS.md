# AGENTS.md

Guidance for AI coding agents working in this repository. See
<https://agents.md/> for the format. Human contributors should read
[README.md](README.md) first, then [TUTORIAL.md](TUTORIAL.md).

## What this project is

A **tutorial** C++ example that implements the BACnet **B-GW (Gateway)** device
profile using the CAS BACnet Stack. It is one of a series - one git repo per
BACnet profile - seeded from B-RTR (Router), with the second physical Network
Port and physical routing removed and DeviceCommunicationControl (DM-DCC-B)
and a **virtual network / virtual device** (GW-VN-B) added instead. It is the
series' canonical example for **F-GATEWAY**. The top priority is that the code
reads like a tutorial a customer can learn from and copy-paste. Favour clarity
over cleverness.

## Layout

This repository is self-contained:

- `main.cpp` - the example device: the real GATEWAY device plus one VIRTUAL
  device it represents.
- `common/` - the shared helper (UDP, callbacks, CLI, keyboard) vendored in.
- `README.md` - what this example is. Keep it short and about THIS example
  only.
- `TUTORIAL.md` - how to extend and review the example, on BOTH devices. Long
  -form material that would bloat the README belongs here.
- `docs/PICS.md` - the Protocol Implementation Conformance Statement, for
  BOTH devices. Its objects-and-properties section is GENERATED from
  `docs/objects.json`; do not hand-edit between the `OBJECTS-PROPERTIES`
  markers.
- `docs/objects.json` - the input to that generator: one entry per object,
  per device (both Device entries, both devices' Network Port objects, and
  each device's other objects). Update it in the same change as any
  `main.cpp` change that adds an object or a `GetProperty*` branch.
- `submodules/cas-bacnet-stack/` - the **CAS BACnet Stack** as a git submodule
  (private; compiled from source). After cloning, run
  `git submodule update --init --recursive`.

The `PROFILE-TABLE` block in README.md is also generated, from the example-series
repository's `docs/profile-table.md`. Edit it there, not here.

## Build

Plain CMake, identical on every platform, in the adapter's default SOURCE mode
(the stack's sources are compiled into the executable - no prebuilt library, no
DLL, no per-platform pre-step):

```bash
git submodule update --init --recursive   # once, if not cloned with --recursive
cmake -B build -S .
cmake --build build --config Release
```

The first build compiles the whole stack (~600 files) and takes a few
minutes; rebuilds after that are incremental and fast. Use
`-D CAS_STACK_DIR=...` only if your stack lives outside the bundled
submodule. Do not reintroduce a link-mode flag or a series-root build script
into the documented build: a customer downloads this repository on its own
and must be able to build it with the two commands above.

## Run

```bash
./build/BACnetExampleBGW [--port 47808] [--deviceID 389019]   # Linux/macOS
.\build\Release\BACnetExampleBGW.exe [--port 47808] [--deviceID 389019]   # Windows
```

Interactive keys while running: `h` help, `q` quit, up/down nudge the
gateway's Analog Input 1, `r` re-send I-Am for both the gateway and the
virtual device.

## Conventions

- Device is named "Chipkin Example B-GW"; objects use the series' colour names; vendor id 389.
- The VIRTUAL device is named "Chipkin Example B-GW (virtual)", instance = the gateway's
  instance + 100 (`docs/colour-table.md`) - **fixed**, not moved by
  `--deviceID` (that flag renumbers only the gateway itself).
- Implement **only** the services and objects the B-GW profile requires - but
  expose **every required property** of each object for Protocol_Revision 24,
  on BOTH devices.
- Outputs are **commandable**: store the 16-slot `Priority_Array` +
  `Relinquish_Default` in the app (the `Commandable` struct); let the stack
  resolve `Present_Value`. Writes land via the `SetProperty*` callbacks (value)
  and `SetPropertyNull` (relinquish). This part is unchanged from B-SA/B-ASC.
  Only the gateway has commandable outputs; the virtual device does not.
- **One socket, two devices.** The virtual device has no Network Port or UDP
  socket of its own - `BACnetStack_AddVirtualNetwork` creates a Network Port
  object for the virtual network internally, and the virtual device is reached
  through the gateway's one physical port. Do not call `SetupUDP` a second
  time or add a second `SetNetworkPortInstance` for it.
- **Dispatch every Get/SetProperty callback on `deviceInstance` first.** Both
  devices have an "Analog Input 1 (Bronze)" at the SAME object type+instance;
  only `deviceInstance` tells them apart. `GetCommandable()` already does this;
  keep the same pattern for anything new.
- **`BACnetStack_AddVirtualNetwork` then `BACnetStack_AddDeviceToVirtualNetwork`,
  in that order, both after `BACnetStack_AddDevice`.** The virtual device
  instance then behaves like any other `deviceInstance` argument to
  `BACnetStack_AddObject`/`SetServiceEnabled`/`SetPropertyEnabled`.
- **Do not add physical routing or a second physical Network Port** - that is
  B-RTR's profile (F-ROUTER/F-MULTIPORT), not this one's. This example claims
  neither NM-RC-B nor DM-LM-B.
- Every `GetProperty*` callback ends with `uint32_t* errorCode`. Leave it alone
  on a catch-all decline (the stack's decline-and-fabricate default answers
  required properties this app does not serve); set it only where this device
  knows the read is wrong (`State_Text` out of range is the one case here, and
  `DeviceCommunicationControl`'s reject paths - see its own comment for why
  that callback is the one exception with no safe catch-all).
- Match the surrounding code style: `const`-correct parameters, check every stack
  return value, keep `main.cpp` linear and well-commented.
- **Never edit `common/` in this repo alone** - it is a vendored copy shared by
  every example in the series, with its own version (`COMMON_VERSION`) and
  changelog (`common/CHANGELOG.md`). To change it: edit it in
  `BACnetProfileExample-B-SS-CPP` (the source of truth) on a branch, bump the
  version, add a changelog entry, open a PR there, merge, THEN re-copy
  `common/` into every example repository including this one.

## How to verify a change

There are no unit tests; verification is behavioural:

1. Build, then run one instance with `--port` on a clear UDP port.
2. With a BACnet client (e.g. bacpypes3/BAC0 or CAS BACnet Explorer), send
   **Who-Is** and confirm **I-Am** from BOTH device instances (the gateway and
   the virtual device).
3. **ReadProperty** every required property of every object on BOTH devices
   and confirm the values; confirm `Protocol_Revision` is 24 on both; confirm
   each device's `Object_List` is separate and correct.
4. **WriteProperty** a gateway commandable output's `Present_Value` at a
   priority, re-read it, then write NULL to relinquish.
5. **DeviceCommunicationControl**: `disable-initiation` then `enable` against
   the gateway; confirm the virtual device does not answer it (not claimed).
6. If you changed the objects or their properties on either device,
   regenerate `docs/PICS.md`
   (`python tools/gen-objects-properties.py BACnetProfileExample-B-GW-CPP` from
   the series root) and confirm no row comes out flagged with ⚠.

## Releasing

Bump `APP_VERSION` in `main.cpp` and add an entry to [CHANGELOG.md](CHANGELOG.md),
then tag `vX.Y.Z`. The GitHub Actions workflow builds and publishes the release.

## License

See [LICENSE](LICENSE). The CAS BACnet Stack is a separate, commercially
licensed product and is not covered by it.

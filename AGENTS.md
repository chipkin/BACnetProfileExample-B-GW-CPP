# AGENTS.md

Guidance for AI coding agents working in this repository. See
<https://agents.md/> for the format. Human contributors should read
[README.md](README.md) first, then [TUTORIAL.md](TUTORIAL.md).

## What this project is

A **tutorial** C++ example that implements the BACnet **B-GW (Gateway)** device
profile using the CAS BACnet Stack. It is one of a series - one git repo per
BACnet profile - and meets the series' **Profile-functional** bar (series
`docs/GLOSSARY.md`): the gateway polls three devices on a foreign bus and
presents their data as BACnet objects, carries BACnet writes back to them, and
reports a foreign device that stops answering (the Failure Mode), all proven by
Wire Tests. It runs as either B-GW: `--mode eo` (GW-EO-B, the default) or
`--mode vn` (GW-VN-B, a virtual device per foreign device on virtual network
100, with the NM-RC-B that requires). The top priority is that the code reads
like a tutorial a customer can learn from and copy-paste. Favour clarity over
cleverness.

The foreign bus is **Simulation** (in-process pretend slaves). It sits behind
ONE seam, `ForeignBus.h`; a real product implements that header with its own
driver (Modbus TCP/RTU, a PLC protocol, ...) and deletes `simulation/`.

## Layout

This repository is self-contained:

- `main.cpp` - the gateway: its objects (both modes), its cache of the foreign
  bus, the poll scheduler and write-through, and the BACnet callbacks.
- `ForeignBus.h` - THE SEAM to the non-BACnet network (asynchronous
  StartRead/StartWrite/CheckTransaction).
- `simulation/` - Simulation, outside the BACnet Surface:
  `SimulatedForeignBus.cpp` implements the seam with three in-memory slaves;
  `SimulationControl.cpp` is the Wire Tests' test-only control socket, opened
  only when the environment variable `BGW_SIMULATION_CONTROL_PORT` is set.
- `tests/` - the Wire Tests (`run_wire_tests.py` runs them in both modes);
  `wiretest/` - the series' shared Wiretest Harness, vendored like `common/`
  (own version + changelog; change it in every example that vendors it).
- `TODO.md` - the known gaps (both in the stack), each reported by the Wire Tests.
- `common/` - the shared helper (UDP, callbacks, CLI, keyboard) vendored in.
- `README.md` - what this example is. Keep it short and about THIS example
  only.
- `TUTORIAL.md` - how to extend and review the example, on BOTH devices. Long
  -form material that would bloat the README belongs here.
- `docs/PICS.md` - the Protocol Implementation Conformance Statement, for
  both modes. Its objects-and-properties section is GENERATED from
  `docs/objects.json`; do not hand-edit between the `OBJECTS-PROPERTIES`
  markers.
- `docs/objects.json` - the input to that generator: one entry per kind of
  object per mode (an entry stands for the three slaves' identical objects). Update it in the same change as any
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
./build/BACnetExampleBGW [--mode eo|vn] [--port 47808] [--deviceID 389019]   # Linux/macOS
.uild\Release\BACnetExampleBGW.exe [--mode eo|vn] [--port 47808]        # Windows
```

Interactive keys while running: `h` help, `q` quit, up/down nudge the
SIMULATED Boiler's sensor (Analog Input 1 follows), `k` kill / revive the
simulated Boiler (`common/` 3.1.0), `r` re-broadcast I-Am-Router-To-Network
(vn) or the gateway's I-Am (eo).

## Conventions

- Device is named "Chipkin Example B-GW"; objects use the series' colour names
  ("Bronze", "Bronze 2", ...); vendor id 389. Virtual devices (vn) are 389119,
  389120, 389121, named "Chipkin Example B-GW (<slave>)" - **fixed**, not moved
  by `--deviceID`.
- **The BACnet Surface is exactly the profile's set**: per slave an Analog
  Input (input register), a commandable Analog Output (holding register) and a
  commandable Binary Output (coil), plus Device and Network Port(s). Do not add
  the B-SS template objects back (Binary Input, Multi-State Input/Output) - the
  Wire Tests check the Object_List sizes.
- **Answer BACnet from the cache, never from the bus.** Callbacks read
  `GatewayedSlave`; `ServiceForeignBus()` (polls, write-through) is the only
  code that talks to `ForeignBus.h`, and nothing blocks.
- **`FindMappedPoint()` is the one place** that turns (deviceInstance, type,
  instance) into a slave and point kind, in both modes. Every callback goes
  through it; dispatch on deviceInstance first.
- Outputs are **commandable**: the app stores the 16-slot `Priority_Array` and
  `Relinquish_Default` (`Commandable`); the stack resolves `Present_Value`.
  `WinningValue()` resolves the same winner only to write it to the slave.
- **Offline**: `OFFLINE_AFTER_FAILED_POLLS` unanswered polls. eo: Reliability
  `communication-failure` (the stack sets FAULT from it). vn: additionally
  Who-Is/Who-Has off for that virtual device. The vn "remain silent" half of
  Annex H.2.2.1 is a stack gap (TODO.md) - do not paper over it.
- **The test control socket is not a CLI option** and must never change BACnet
  behaviour; it only drives the simulation. Keep it env-var-only.
- Every `GetProperty*` callback ends with `uint32_t* errorCode`. Leave it alone
  on a catch-all decline (the stack's decline-and-fabricate default answers
  required properties this app does not serve); set it only where this device
  knows the request is wrong.
- Match the surrounding code style: `const`-correct parameters, check every stack
  return value, descriptive names (no single letters except a plain loop `i`).
- **Never edit `common/` in this repo alone** - it is a vendored copy shared by
  every example in the series, with its own version (`COMMON_VERSION`) and
  changelog (`common/CHANGELOG.md`). Edit it in `BACnetProfileExample-B-SS-CPP`
  (the source of truth), bump, changelog, then re-copy. Same for `wiretest/`
  (its source is whichever example last bumped `WIRETEST_VERSION`).

## How to verify a change

```bash
python3 tests/run_wire_tests.py --exe build/BACnetExampleBGW   # Linux/macOS; both modes
```

Must end `ALL WIRE TESTS PASSED`. A new behaviour gets a new check, and the
check must be **seen to fail with the behaviour removed** (tests/README.md has
the record for every existing one). If you changed objects or properties,
regenerate `docs/PICS.md` (`python tools/gen-objects-properties.py
BACnetProfileExample-B-GW-CPP` from the series root) and confirm no row comes
out flagged with ⚠.

## Releasing

Bump `APP_VERSION` in `main.cpp` and add an entry to [CHANGELOG.md](CHANGELOG.md),
then tag `vX.Y.Z`. The GitHub Actions workflow builds and publishes the release.

## License

See [LICENSE](LICENSE). The CAS BACnet Stack is a separate, commercially
licensed product and is not covered by it.

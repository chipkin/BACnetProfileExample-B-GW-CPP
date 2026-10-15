# BACnet B-GW (Gateway) - C++ example

A copy-paste-friendly example of the BACnet **B-GW (Gateway)** device profile in
C++, using the [CAS BACnet Stack](https://store.chipkin.com/services/stacks/bacnet-stack).

**What it does:** the gateway polls three devices on a foreign (non-BACnet)
bus and presents their data as BACnet objects. Reads follow the foreign
values, writes are carried through to the foreign devices, and a foreign
device that stops answering is reported, not hidden. It runs as either kind of
gateway Annex L.7.2 allows:

- **`--mode eo`** (the default) - **GW-EO-B**: the foreign data are objects
  inside the gateway device.
- **`--mode vn`** - **GW-VN-B**: each foreign device becomes its own virtual
  BACnet device on a virtual network behind the gateway, which acts as a
  router to it (NM-RC-B).

> **The foreign bus is Simulation.** There is no real field bus here: three
> pretend field devices ("slaves") live inside the process
> ([`simulation/`](simulation/)), each with a small Modbus-style register map,
> its own poll cycle and response latency, and a sensor that drifts. The
> gateway reaches them only through one interface, [`ForeignBus.h`](ForeignBus.h)
> - **the seam**. A real product deletes `simulation/` and implements
> `ForeignBus.h` with its own driver: **Modbus TCP or Modbus RTU**, M-Bus, a
> PLC's native protocol (EtherNet/IP, S7, ...), a vendor's serial protocol, a
> REST API - any BACnet-unaware system. Everything a BACnet client sees is
> real BACnet behaviour of the CAS BACnet Stack; only the far side of the
> gateway is pretend.

**[Download a prebuilt binary](https://github.com/chipkin/BACnetProfileExample-B-GW-CPP/releases)**
(Windows and Linux x64) - or build it yourself, see [Build](#build) below.

- **[TUTORIAL.md](TUTORIAL.md)** - how to put your own driver behind the seam,
  extend the register map, and review the example for conformance.
- **[docs/PICS.md](docs/PICS.md)** - the Protocol Implementation Conformance
  Statement: every object in both modes, every property, and who answers it.
- **[tests/](tests/README.md)** - the Wire Tests that prove the gateway works,
  from outside it, over BACnet/IP.

> **Versions:** this document describes **example v2.0.0**, built and
> verified against **CAS BACnet Stack 6.0.23** (`6.x` @ `e6de4ffd`), at
> **Protocol_Revision 30**, with the vendored `common/` helper at **v3.2.0**
> and `wiretest/` at **v0.3.0**. Running the example prints the first three -
> if what it prints disagrees with this line, trust the program and check
> `CHANGELOG.md`.

## What is a B-GW (Gateway) profile?

**B-GW** (Annex L.7.2, Miscellaneous) is "intended for connecting one or more
non-BACnet networks into a BACnet internetwork". Annex H.1 describes the ways
to do it; Annex K.7 turns two of them into BIBBs:

| BIBB | Annex | What the gateway does | This example |
|---|---|---|---|
| **GW-EO-B** (Embedded Objects) | K.7.2, H.1.1.3 | The foreign devices' data are objects in the gateway's own device. Requires DS-RP-B. | `--mode eo` |
| **GW-VN-B** (Virtual Network) | K.7.1, H.1.1.1 | Each foreign device is a virtual BACnet device on a virtual network; the gateway "appears to the BACnet network as a router". Requires NM-RC-B and DS-RP-B. | `--mode vn` |

A B-GW claims one or the other (Annex L.7 table footnote 1), so one run of the
example is one B-GW. Annex H.2 then lists what a gateway must get right, and
this example demonstrates each one it can:

| Annex H.2 | Requirement | Where |
|---|---|---|
| H.2.1.1 | A write may be acknowledged before it reaches the foreign device (write caching). | Writes are acknowledged at once and written through on the next pass; retried until they land. |
| H.2.1.3 | Priority_Array implemented correctly with all 16 entries. | Analog Output / Binary Output. |
| H.2.1.4 | Never keep a BACnet client waiting on the foreign bus. | Every answer comes from the gateway's cache; the bus is polled asynchronously. |
| H.2.3.2 (eo) | A communication error shows in Reliability and the FAULT flag; reads still return values. | Kill a slave: Reliability = `communication-failure`, FAULT set, values still read. |
| H.2.2.1 (vn) | An offline foreign device's virtual device sends no I-Am and stays silent. | No I-Am: done. Silent: **blocked on the stack**, see [Known gaps](#known-gaps). |

## The foreign bus (Simulation) and its register map

Three slaves, all with the same register map (addresses from 0, Modbus
convention; temperatures are degrees Celsius x 10, so `652` = 65.2 C):

| Unit | Slave | Poll cycle | Latency | Input register 0 | Holding register 0 | Coil 0 |
|:---:|---|---:|---:|---|---|---|
| 1 | Boiler | 500 ms | 20 ms | supply temperature (starts 65.2, drifts) | supply setpoint (65.0) | burner enable (1) |
| 2 | Air Handler | 700 ms | 45 ms | discharge temperature (13.1, drifts) | discharge setpoint (13.0) | fan enable (1) |
| 3 | Chiller | 900 ms | 80 ms | chilled-water temperature (7.1, drifts) | chilled-water setpoint (7.0) | compressor enable (0) |

A transaction to a slave that is offline times out after 300 ms; two
unanswered polls in a row and the gateway declares the slave offline. The
**`k`** key pulls the Boiler's cable (and plugs it back in) so you can watch
the Failure Mode.

How each column becomes BACnet:

| Foreign point | BACnet object (`--mode eo`, on Device 389019) | BACnet object (`--mode vn`, on the slave's virtual device) |
|---|---|---|
| Unit *n* input register 0 | Analog Input *n* "Bronze *n*" - Present_Value follows the register | Analog Input 1 "Bronze" |
| Unit *n* holding register 0 | Analog Output *n* "Chartreuse *n*" - commandable; the winning value is written to the register | Analog Output 1 "Chartreuse" |
| Unit *n* coil 0 | Binary Output *n* "Fuchsia *n*" - commandable; written to the coil | Binary Output 1 "Fuchsia" |

(Instance 1's names are plain "Bronze", "Chartreuse", "Fuchsia", per the
series colour table.) Every mapped object has Reliability, Status_Flags and a
Description naming its slave and register.

## The devices this example creates

`--mode eo` (default) - one device:

```
Device 389019  "Chipkin Example B-GW"                  (Vendor 389, Chipkin Automation Systems)
    +-- Analog Input  1, 2, 3   "Bronze", "Bronze 2", "Bronze 3"              <- units 1-3 input register 0
    +-- Analog Output 1, 2, 3   "Chartreuse", "Chartreuse 2", "Chartreuse 3"  -> units 1-3 holding register 0
    +-- Binary Output 1, 2, 3   "Fuchsia", "Fuchsia 2", "Fuchsia 3"           -> units 1-3 coil 0
    +-- Network Port 1          "Vermilion"                                  BACnet/IP
```

`--mode vn` - the gateway plus one virtual device per slave on virtual network 100:

```
Device 389019  "Chipkin Example B-GW"                  (the gateway - a router to network 100)
    +-- Network Port 1          "Vermilion"            BACnet/IP
    +-- Network Port 2          "Vermilion 2"          virtual network 100 (created by the stack)

Device 389119  "Chipkin Example B-GW (Boiler)"         (virtual, network 100, unit 1)
Device 389120  "Chipkin Example B-GW (Air Handler)"    (virtual, network 100, unit 2)
Device 389121  "Chipkin Example B-GW (Chiller)"        (virtual, network 100, unit 3)
    each:
    +-- Analog Input  1         "Bronze"               <- input register 0
    +-- Analog Output 1         "Chartreuse"           -> holding register 0
    +-- Binary Output 1         "Fuchsia"              -> coil 0
    +-- Network Port 1          "Vermilion"            virtual network 100 (created by the stack)
```

The B-SS template objects every other example in the series carries (Binary
Input "Emerald", Multi-State Input "Hot Pink", Multi-State Output "Indigo")
are **not** here: they would map to nothing, and a profile example's BACnet
Surface is exactly what the profile needs. Analog Input "Bronze", Analog
Output "Chartreuse" and Binary Output "Fuchsia" are kept, mapped to the bus.

**Reaching a virtual device.** In `--mode vn` a virtual device shares the
gateway's UDP socket but lives on network 100, so a client addresses it the
way it would any device behind a router: an NPDU with **DNET = 100** and
**DADR = the virtual device's address**, which the stack makes the 3-octet
device instance (`05 EF FF` for 389119). A routing-aware client learns it from
the I-Am the virtual device answers a Who-Is with (it carries SNET 100 and that
SADR) - nothing example-specific.

## What this example supports

### BIBBs (BACnet Interoperability Building Blocks)

| BIBB | Description | `--mode eo` | `--mode vn` |
|------|-------------|:---------:|:---------:|
| DS-RP-B | Data Sharing - ReadProperty - B | ✅ | ✅ |
| DS-WP-B | Data Sharing - WriteProperty - B | ✅ | ✅ (on the virtual devices' outputs) |
| DM-DDB-B | Device Management - Dynamic Device Binding - B | ✅ | ✅ |
| DM-DOB-B | Device Management - Dynamic Object Binding - B | ✅ | ✅ |
| DM-DCC-B | Device Management - Device Communication Control - B | ✅ (gateway) | ✅ (gateway) |
| GW-EO-B | Gateway - Embedded Objects - B | ✅ | - |
| GW-VN-B | Gateway - Virtual Network - B | - | ✅ (one gap: [Known gaps](#known-gaps)) |
| NM-RC-B | Network Management - Router Configuration - B | - | ✅ Who-Is-Router-To-Network answered, verified on the wire (one stack gap: [Known gaps](#known-gaps)) |

✅ here means **proved by a Wire Test** ([tests/](tests/README.md)), not read
from source. DeviceCommunicationControl is answered by the gateway only; the
virtual devices do not claim it.

This example does not implement DM-LM-B, physical inter-datalink routing (that
is B-RTR), alarming, scheduling or trending. Annex H.2.3.2 says a gateway
"should support initiation of event notifications" for faults - optional, and
not shown here; the fault is visible in Reliability and Status_Flags.

### Services (executed / B-side)

| Service | Notes |
|---------|-------|
| ReadProperty | Gateway and virtual devices; answered from the gateway's cache. |
| WriteProperty | Present_Value of the mapped Analog/Binary Outputs, at a priority; NULL relinquishes. |
| DeviceCommunicationControl | Gateway only. |
| Who-Is / I-Am | Gateway; in `--mode vn` every ONLINE virtual device (an offline one stops answering). |
| Who-Has / I-Have | Same as Who-Is. |
| Who-Is-Router-To-Network | `--mode vn`: answered with I-Am-Router-To-Network(100) by the stack's virtual router. |

## Known gaps

Both are in the CAS BACnet Stack, not this example; each is reported as
`[KNOWN GAP]` on every Wire Test run and listed in [TODO.md](TODO.md).

1. **`--mode vn`: an offline virtual device is not silent (Annex H.2.2.1) -
   blocked on the stack.** H.2.2.1: communication timeouts "shall not result
   in the gateway returning an error, abort, or reject PDU in response to
   BACnet requests directed to the non-BACnet device. Instead, the gateway
   shall remain silent." The stack has no customer export to silence a virtual
   device: `BACnetStack_RemoveDevice` is one-way (the instance can never be
   added back), and disabling ReadProperty answers with a Reject - an answer.
   What the example does today: it stops the virtual device's I-Am/I-Have
   (that part of H.2.2.1 works) and keeps answering reads from the cache with
   Reliability = `communication-failure` and the FAULT flag set - honest, but
   not the silence the clause asks for.
2. **I-Am-Router-To-Network is sent unicast.** Cl. 6.4.2 says it shall
   "always be transmitted with a broadcast MAC address"; the stack's answer to
   Who-Is-Router-To-Network goes back to the requester only (the same stack
   defect B-RTR reports). The content - network 100 - is correct.

## Requires the CAS BACnet Stack (licensed product)

This example **builds against the CAS BACnet Stack, which is a commercial Chipkin
product** - it is not free or open source, and there is no public/trial build.
The stack is referenced here as the **private** git submodule
`submodules/cas-bacnet-stack`; you can only fetch and build it once you have a CAS
BACnet Stack license and access to that repository.

**To get the CAS BACnet Stack (and access to build this example), contact
Chipkin:** <https://store.chipkin.com/services/stacks/bacnet-stack> or
sales@chipkin.com.

You do not need a stack licence to *read* this example, or to run a
[prebuilt release binary](https://github.com/chipkin/BACnetProfileExample-B-GW-CPP/releases).
The licence is what lets you *build* it - that is the part the stack submodule
gates.

## What's in this repository

This is a **self-contained** project. It ships:

- `main.cpp` - the gateway: its objects, its cache of the foreign bus, the poll
  scheduler and write-through, and the BACnet callbacks that answer from the cache.
- `ForeignBus.h` - **the seam** between the gateway and the non-BACnet network.
- `simulation/` - **Simulation**, not part of the product: the pretend field
  bus (`SimulatedForeignBus.cpp`, which implements `ForeignBus.h`) and the
  Wire Tests' test-only control socket (`SimulationControl.cpp`). A real
  product deletes this folder and implements `ForeignBus.h` with its own driver.
- `tests/` - the Wire Tests; `wiretest/` - the series' shared Wiretest Harness
  they are written against (vendored, like `common/`).
- `common/` - the shared helper (UDP, callbacks, CLI, keyboard) vendored in.
- `CMakeLists.txt` - the build, the same on Windows, Linux, and macOS.
- `docs/PICS.md` - the conformance statement.
- `submodules/cas-bacnet-stack/` - the **CAS BACnet Stack as a git submodule**
  (private; requires a license - see above). Its sources are compiled into the
  executable, so there is no library or DLL to build, ship, or install.

## Prerequisites

- A C++17 compiler (MSVC, GCC, or Clang).
- CMake >= 3.15.
- Git (to fetch the stack submodule).
- Python 3 (standard library only) - only to run the Wire Tests.

### Windows

- **C++ compiler** - install
  [Visual Studio Community](https://visualstudio.microsoft.com/downloads/)
  (free) and select the **"Desktop development with C++"** workload.
- **CMake** - from <https://cmake.org/download/>, or `winget install Kitware.CMake`.

### Linux / macOS

- Debian/Ubuntu: `sudo apt install build-essential cmake git python3`
- macOS: `xcode-select --install` and `brew install cmake`

## Build

CMake only, and the same two commands on every platform:

```bash
git clone --recursive https://github.com/chipkin/BACnetProfileExample-B-GW-CPP.git
cd BACnetProfileExample-B-GW-CPP

cmake -B build -S .
cmake --build build --config Release
```

Already cloned without `--recursive`? Run `git submodule update --init --recursive`
first - the build needs the stack submodule.

> **The first build takes a few minutes** - it compiles the entire CAS BACnet
> Stack (~600 source files) into the executable. Rebuilds after that are
> incremental and take seconds.

If your CAS BACnet Stack lives somewhere other than the bundled submodule, point
CMake at it: `cmake -B build -S . -D CAS_STACK_DIR=/path/to/cas-bacnet-stack`.

## Run

```bash
# Linux / macOS
./build/BACnetExampleBGW --port 47808              # --mode eo, the default
./build/BACnetExampleBGW --port 47808 --mode vn

# Windows
.\build\Release\BACnetExampleBGW.exe --port 47808 --mode eo
```

Expected output (`--mode eo`):

```
BACnet B-GW (Gateway) Example - C++ v2.0.0
CAS BACnet Stack version: 6.0.23.0
Common helper (common/) version: 3.2.0
Mode: eo - GW-EO-B, the slaves' data as objects in the gateway
FYI: Listening for BACnet/IP on UDP port 47808 (Network Port 1).
TX 21 bytes to 192.168.3.255:47808 (broadcast) (Network Port 1) - Unconfirmed: I-Am
FYI: Device 389019 ("Chipkin Example B-GW") ready. Vendor ID 389. Gatewaying 3 slaves on a SIMULATED foreign bus (--mode eo: objects in this device). Press 'h' for help.
GATEWAY: wrote 65.0 to slave 1 holding register 0.
GATEWAY: wrote 1.0 to slave 1 coil 0.
...
```

The `GATEWAY: wrote` lines are the outputs' Relinquish_Default values going to
the slaves at start-up: Annex H.2.1.2 recommends Output objects for foreign
outputs "controlled only through BACnet", so the BACnet side owns the value. The device listens on UDP **47808** by default; allow it through your
firewall.

> **A red `Error:` line about a UUID not being set for a BACnet/SC datalink
> is expected and is not your bug** - the stack starts a BACnet/SC datalink
> this BACnet/IP-only example never configures. [TUTORIAL.md](TUTORIAL.md#troubleshooting)
> explains this and other benign start-up messages.

### Command-line options

| Option | Default | Meaning |
|--------|---------|---------|
| `--mode eo\|vn` | `eo` | `eo`: GW-EO-B, the slaves' data as objects in the gateway. `vn`: GW-VN-B, each slave a virtual device on virtual network 100. Anything else is refused. |
| `--port <n>` | `47808` | UDP port for Network Port 1 (Vermilion). |
| `--deviceID <n>` | `389019` | The GATEWAY's instance. **Does not move the virtual devices** (`389119`-`389121`). |
| `--help`, `-h` | - | Show usage and exit. |
| `--version` | - | Print the example, stack, and `common/` helper versions, then exit. |

(`--help` also lists `--dcc-password`, a `common/` option this example does not
read; its DeviceCommunicationControl password is the `DCC_PASSWORD` constant.)

### Interactive commands

| Key | Action |
|-----|--------|
| `h` | Show the version information and the command list. |
| `q` | Quit. |
| up / down arrow | Nudge the **simulated** Boiler's sensor by +/-1.1 C; Analog Input 1 follows on the next poll. |
| `k` | Kill / revive the **simulated** Boiler (slave 1) - its objects go to fault, then recover (`common/` 3.1.0). |
| `r` | `--mode vn`: re-broadcast I-Am-Router-To-Network(100). `--mode eo` is not a router: re-sends the gateway's I-Am. |

## Wire Tests

The proof that the gateway does its job is automated, and runs in CI after the
smoke test (Linux). Python 3, standard library only:

```bash
python3 tests/run_wire_tests.py --exe build/BACnetExampleBGW
```

It runs each script below in **both** modes and ends with
`ALL WIRE TESTS PASSED`. The tests drive the gateway over BACnet/IP like any
client, and drive the simulated bus through a test-only control socket that
is switched on by an environment variable (`BGW_SIMULATION_CONTROL_PORT`) -
never by a command-line option, because it changes nothing the gateway does on
the BACnet network. [tests/README.md](tests/README.md) has every check, and
the record of each one failing when its feature is removed.

| Script | What it proves |
|---|---|
| `mapping_test.py` | The BACnet Surface is exactly the mapped objects (Object_List sizes); Analog Input follows its input register; WriteProperty to Analog Output reaches the holding register at the winning priority; relinquishing every slot falls back to Relinquish_Default; a value the register cannot hold is refused; Binary Output reaches the coil; all 16 Priority_Array entries. |
| `failure_mode_test.py` | Kill a slave: eo - Reliability `communication-failure` + FAULT on its three objects (only its), values still read, writes still accepted and delivered after revive; vn - no I-Am for its virtual device. Revive: everything recovers. |
| `router_test.py` | vn: Who-Is-Router-To-Network gets I-Am-Router-To-Network(100); a ReadProperty routed to DNET 100 reaches a virtual device and comes back with SNET 100. eo (the control): no router answer at all, while the gateway still answers a ReadProperty. |

Last local run (2026-10-14, WSL Ubuntu 22.04, STATIC build of stack `e6de4ffd`):
eo 24/24, 15/15, 2/2; vn 27/27, 19/19, 10/10 - `ALL WIRE TESTS PASSED`, with the
two [Known gaps](#known-gaps) reported.

## Verify by hand

With a BACnet client such as the
[CAS BACnet Explorer](https://store.chipkin.com/products/tools/cas-bacnet-explorer)
(routing-aware, for `--mode vn`):

1. `--mode eo`: discover Device 389019; read Analog Input 1 "Bronze" - about
   65.2, moving by 0.1 C a second (the simulated sensor drifts). Press up: it
   follows within half a second.
2. Write Analog Output 1 "Chartreuse" = 70 at priority 8: the console prints
   `GATEWAY: wrote 70.0 to slave 1 holding register 0.` Write NULL at priority
   8: it writes 65.0 (Relinquish_Default) back.
3. Press `k`: within about 1.5 seconds Analog Input 1, Analog Output 1 and Binary
   Output 1 read Reliability `communication-failure` and Status_Flags FAULT;
   "Bronze 2" and "Bronze 3" stay normal. Press `k` again: they recover.
4. `--mode vn`: discover - four devices answer (389019 and the virtual 389119,
   389120, 389121 on network 100). Press `k`: a fresh Who-Is no longer finds
   389119.

For a property-by-property review against the conformance statement, see
[TUTORIAL.md](TUTORIAL.md).

## The BACnet profile example series

<!-- PROFILE-TABLE:BEGIN (generated from cas-bacnet-stack-examples/docs/profile-table.md - do not edit here) -->
There is one example repository per standardized device profile in ASHRAE 135-2024 Annex L. An example marked ⛔ is blocked: the stack cannot yet perform that profile's function through its customer interface, and the example's README says what it does today. Pick the profile your device claims, then the language you build in. "Ask" means the example hasn't been built yet for that language - [contact Chipkin](https://store.chipkin.com/contact-us) if you need one.

### Controllers (Annex L.4)

| Profile | C++ | Node.js | C# | Rust | Python | Go |
|---|---|---|---|---|---|---|
| **B-SS** Smart Sensor | [B-SS-CPP](https://github.com/chipkin/BACnetProfileExample-B-SS-CPP) | [B-SS-Node](https://github.com/chipkin/BACnetProfileExample-B-SS-Node) | [B-SS-CS](https://github.com/chipkin/BACnetProfileExample-B-SS-CS) | [B-SS-Rust](https://github.com/chipkin/BACnetProfileExample-B-SS-Rust) | [B-SS-Python](https://github.com/chipkin/BACnetProfileExample-B-SS-Python) | [B-SS-Go](https://github.com/chipkin/BACnetProfileExample-B-SS-Go) |
| **B-SA** Smart Actuator | [B-SA-CPP](https://github.com/chipkin/BACnetProfileExample-B-SA-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-ASC** Application Specific Controller | [B-ASC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ASC-CPP) | [B-ASC-Node](https://github.com/chipkin/BACnetProfileExample-B-ASC-Node) | Ask | Ask | Ask | Ask |
| **B-AAC** Advanced Application Controller | [B-AAC-CPP](https://github.com/chipkin/BACnetProfileExample-B-AAC-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-BC** Building Controller | [B-BC-CPP](https://github.com/chipkin/BACnetProfileExample-B-BC-CPP) | Ask | Ask | Ask | Ask | Ask |

### Life safety controllers (Annex L.5)

| Profile | C++ | Node.js | C# | Rust | Python | Go |
|---|---|---|---|---|---|---|
| **B-LSC** Life Safety Controller | [B-LSC-CPP](https://github.com/chipkin/BACnetProfileExample-B-LSC-CPP) 🚧 | Ask | Ask | Ask | Ask | Ask |
| **B-ALSC** Advanced Life Safety Controller | [B-ALSC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ALSC-CPP) | Ask | Ask | Ask | Ask | Ask |

### Access control controllers (Annex L.6)

| Profile | C++ | Node.js | C# | Rust | Python | Go |
|---|---|---|---|---|---|---|
| **B-ACC** Access Control Controller | [B-ACC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ACC-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-AACC** Advanced Access Control Controller | [B-AACC-CPP](https://github.com/chipkin/BACnetProfileExample-B-AACC-CPP) | Ask | Ask | Ask | Ask | Ask |

### Lighting controllers (Annex L.11)

| Profile | C++ | Node.js | C# | Rust | Python | Go |
|---|---|---|---|---|---|---|
| **B-LD** Lighting Device | [B-LD-CPP](https://github.com/chipkin/BACnetProfileExample-B-LD-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-LS** Lighting Supervisor | [B-LS-CPP](https://github.com/chipkin/BACnetProfileExample-B-LS-CPP) | Ask | Ask | Ask | Ask | Ask |

### Elevator controllers (Annex L.13)

| Profile | C++ | Node.js | C# | Rust | Python | Go |
|---|---|---|---|---|---|---|
| **B-EM** Elevator Monitor | [B-EM-CPP](https://github.com/chipkin/BACnetProfileExample-B-EM-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-EC** Elevator Controller | [B-EC-CPP](https://github.com/chipkin/BACnetProfileExample-B-EC-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-AEC** Advanced Elevator Controller | [B-AEC-CPP](https://github.com/chipkin/BACnetProfileExample-B-AEC-CPP) | Ask | Ask | Ask | Ask | Ask |

### Authentication and authorization (Annex L.14)

| Profile | C++ | Node.js | C# | Rust | Python | Go |
|---|---|---|---|---|---|---|
| **B-AS** Authorization Server | [B-AS-CPP](https://github.com/chipkin/BACnetProfileExample-B-AS-CPP) | Ask | Ask | Ask | Ask | Ask |

### Miscellaneous (Annex L.7, combinable with any one family)

| Profile | C++ | Node.js | C# | Rust | Python | Go |
|---|---|---|---|---|---|---|
| **B-BBMD** Broadcast Management Device | [B-BBMD-CPP](https://github.com/chipkin/BACnetProfileExample-B-BBMD-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-ACDC** Access Control Door Controller | [B-ACDC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ACDC-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-ACCR** Access Control Credential Reader | [B-ACCR-CPP](https://github.com/chipkin/BACnetProfileExample-B-ACCR-CPP) ⛔ blocked: credential format not reportable (no stack issue yet) | Ask | Ask | Ask | Ask | Ask |
| **B-RTR** Router | [B-RTR-CPP](https://github.com/chipkin/BACnetProfileExample-B-RTR-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-GW** Gateway | [B-GW-CPP](https://github.com/chipkin/BACnetProfileExample-B-GW-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-DAP** Device Address Proxy | [B-DAP-CPP](https://github.com/chipkin/BACnetProfileExample-B-DAP-CPP) ⛔ blocked on [stack #2032](https://github.com/chipkin/cas-bacnet-stack/issues/2032) | Ask | Ask | Ask | Ask | Ask |
| **B-SCHUB** BACnet/SC Hub | [B-SCHUB-CPP](https://github.com/chipkin/BACnetProfileExample-B-SCHUB-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-GENERAL** General device (Annex L.8) | *(satisfied by every example above)* | — | — | — | — | — |

### Operator interfaces and workstations (Annex L.1–L.3, L.9–L.10, L.12)

Client-side profiles.

| Profile | C++ | Node.js | C# | Rust | Python | Go |
|---|---|---|---|---|---|---|
| **B-OD** Operator Display | [B-OD-CPP](https://github.com/chipkin/BACnetProfileExample-B-OD-CPP) | Ask | Ask | Ask | Ask | Ask |
| **B-OWS** Operator Workstation | planned | — | — | — | — | — |
| **B-AWS** Advanced Operator Workstation | planned | — | — | — | — | — |
| **B-XAWS** Extended Advanced Operator Workstation | planned | — | — | — | — | — |
| **B-LSAP** Life Safety Annunciator Panel | planned | — | — | — | — | — |
| **B-LSWS** Life Safety Workstation | planned | — | — | — | — | — |
| **B-ALSWS** Advanced Life Safety Workstation | planned | — | — | — | — | — |
| **B-ACSD** Access Control Security Display | planned | — | — | — | — | — |
| **B-ACWS** Access Control Workstation | planned | — | — | — | — | — |
| **B-AACWS** Advanced Access Control Workstation | planned | — | — | — | — | — |
| **B-LOD** Lighting Operator Display | planned | — | — | — | — | — |
| **B-ALWS** Advanced Lighting Workstation | planned | — | — | — | — | — |
| **B-LCS** Lighting Control Station | planned | — | — | — | — | — |
| **B-ALCS** Advanced Lighting Control Station | planned | — | — | — | — | — |
| **B-ED** Elevator Display | planned | — | — | — | — | — |
| **B-EWS** Elevator Workstation | planned | — | — | — | — | — |
| **B-AEWS** Advanced Elevator Workstation | planned | — | — | — | — | — |

🚧 = in progress. ⛔ = Blocked Example (blocked on the named stack gap). "Ask" = not yet built for that language; contact Chipkin if you need it. Profile definitions: ANSI/ASHRAE 135-2024 Annex L. BIBB definitions: Annex K. Get the stack: <https://store.chipkin.com/services/stacks/bacnet-stack>.
<!-- PROFILE-TABLE:END -->

## References

- **ANSI/ASHRAE Standard 135** (BACnet) - the protocol standard. Object model
  (Clause 12), services (Clause 15), BACnet/IP (Annex J), device profiles
  (Annex L). Purchase / preview via the [ASHRAE store](https://www.ashrae.org/technical-resources/standards-and-guidelines).
- **What is BACnet?** - Chipkin's introduction:
  <https://docs.chipkin.com/protocols/bacnet/>.
- **CAS BACnet Stack** - product page and documentation:
  <https://store.chipkin.com/services/stacks/bacnet-stack>.
- **CAS BACnet Explorer** - client for testing this device:
  <https://store.chipkin.com/products/tools/cas-bacnet-explorer>.
- **Shared helper used by this example** - [`common/README.md`](common/README.md).

See also [TUTORIAL.md](TUTORIAL.md), [docs/PICS.md](docs/PICS.md),
[CHANGELOG.md](CHANGELOG.md), and [AGENTS.md](AGENTS.md).

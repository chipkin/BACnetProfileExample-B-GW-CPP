# BACnet B-GW (Gateway) - C++ example

A minimal, copy-paste-friendly example showing how to implement the BACnet
**B-GW (Gateway)** device profile in C++ using the
[CAS BACnet Stack](https://store.chipkin.com/services/stacks/bacnet-stack).
It is a single process that is simultaneously **two BACnet devices**: a real
gateway device reachable directly over BACnet/IP, and **one virtual BACnet
device** the gateway represents on a virtual network behind it - the pattern
a real gateway uses to expose a non-BACnet (or otherwise not-directly-BACnet)
device to a BACnet internetwork. It listens on **BACnet/IP (UDP 47808)**,
answers **ReadProperty** and **WriteProperty** requests, and is discoverable
via **Who-Is / I-Am**.

**[Download a prebuilt binary](https://github.com/chipkin/BACnetProfileExample-B-GW-CPP/releases)**
(Windows and Linux x64) - or build it yourself, see [Build](#build) below.

- **[TUTORIAL.md](TUTORIAL.md)** - how to extend this example and how to
  review it for conformance. Read it when you start turning this into your
  own gateway.
- **[docs/PICS.md](docs/PICS.md)** - the Protocol Implementation Conformance
  Statement: every object on both devices, every property, and who answers
  it.

> **Versions:** this document describes **example v1.0.0**, built and
> verified against **CAS BACnet Stack 6.0.21** (`6.x` @ `abd4cee1`), at
> **Protocol_Revision 24**, with the vendored `common/` helper at **v2.5.0**.
> Running the example prints all three - if what it prints disagrees with
> this line, trust the program and check `CHANGELOG.md`.

## What is a B-GW (Gateway) profile?

**B-GW (Gateway)** (Annex L.7, Miscellaneous, combinable with any other
profile family) is a device that represents a device it is gatewaying for -
typically a device on a non-BACnet network, or one this device otherwise
brokers access to - as a **virtual BACnet device** on a **virtual network**
it owns. Annex K.7 defines two ways a gateway proves this: **GW-VN-B**
(virtual network - the one this example implements) or **GW-EO-B** (embedded
objects - representing the other device's data as objects on the gateway's
OWN device instead of a separate virtual device). This example implements
**GW-VN-B**: a genuinely separate virtual Device object, with its own
`Object_List`, discoverable and readable in its own right.

## The devices this example creates

```
Device 389019  "Chipkin Example B-GW"              (the GATEWAY - Vendor 389, Chipkin Automation Systems)
    |
    +-- Analog Input  1       "Bronze"       Present_Value  21.5      (REAL, degrees Celsius; read-only)
    +-- Binary Input  1       "Emerald"      Present_Value  inactive  (0 = inactive / 1 = active; read-only)
    +-- Multi-State Input 1   "Hot Pink"     Present_Value  1         (state, 1..3; read-only)
    +-- Analog Output 1       "Chartreuse"   Present_Value  20.0      (REAL setpoint; WRITABLE, commandable)
    +-- Binary Output 1       "Fuchsia"      Present_Value  inactive  (0/1; WRITABLE, commandable)
    +-- Multi-State Output 1  "Indigo"       Present_Value  1         (state, 1..3; WRITABLE, commandable)
    +-- Network Port 1        "Vermilion"    BACnet/IP                (the one physical port)
    +-- Network Port 2        (unnamed)      virtual network 100      (created internally by AddVirtualNetwork)

Device 389119  "Chipkin Example B-GW (virtual)"     (the VIRTUAL device, on virtual network 100)
    |
    +-- Analog Input  1       "Bronze"       Present_Value  19.0      (REAL, degrees Celsius; read-only;
                                                                        its OWN value, independent of the
                                                                        gateway's Analog Input 1 above)
    +-- Network Port 1        (unnamed)      virtual network 100      (created internally by
                                                                        AddDeviceToVirtualNetwork)
```

The gateway's three **input** objects and three **output** objects are the
shared minimum/commandable pattern every example in this series carries. The
**virtual device** is this example's own contribution to the pattern.

## How the virtual network works

At start-up, `main.cpp` (after creating the gateway device, its objects, and
its Network Port):

1. `BACnetStack_AddVirtualNetwork(389019, network 100, virtual Network Port
   instance 2)` - turns the gateway into a virtual router and creates a second
   Network Port object (fully populated by the stack; nothing for this
   application's callbacks to serve) representing the virtual network.
2. `BACnetStack_AddDeviceToVirtualNetwork(389119, network 100)` - adds the
   virtual device itself. From this point on, `389119` is a full device as far
   as the rest of the API is concerned: `BACnetStack_AddObject`,
   `SetServiceEnabled`, `SetPropertyEnabled` all take it directly as their
   `deviceInstance` argument, exactly like the gateway's own `389019`.
3. Enables ReadProperty and discovery (Who-Is/I-Am, Who-Has/I-Have) on the
   virtual device, and adds its one object: Analog Input 1 ("Bronze"), with its
   own, independent `Present_Value`.
4. Broadcasts an unsolicited **I-Am** for BOTH devices at start-up (and on the
   `r` key), both from the gateway's one Network Port - the virtual device has
   no socket of its own.

**Addressing the virtual device from a client.** The virtual device shares the
gateway's one UDP socket; it is NOT reachable by a plain, unrouted ReadProperty
to that socket - a request naming the virtual device's own `Object_Identifier`
gets `unknown-object`, because the virtual device's objects live on the
*virtual* network, not the gateway's own local one. A client must address it
exactly as it would a device behind a real router: an NPDU carrying **DNET =
100** and the virtual device's MAC address on that network, which the CAS
BACnet Stack derives from the device instance itself. A routing-aware BACnet
client that has learned this address from the virtual device's own I-Am
(which carries its network-layer source address) or from
Who-Is-Router-To-Network addresses it correctly without the operator doing
anything special - standard BACnet internetwork behaviour (ASHRAE 135 cl.
6.6), not an example-specific quirk. See [TUTORIAL.md](TUTORIAL.md) for the
implementation notes on how this differs from physical inter-datalink
routing.

## What this example supports

The example implements exactly the capabilities below - and nothing more,
which is the point of a profile example.

### BIBBs (BACnet Interoperability Building Blocks)

| BIBB | Description | Supported |
|------|-------------|:---------:|
| DS-RP-B | Data Sharing - ReadProperty - B | ✅ |
| DS-WP-B | Data Sharing - WriteProperty - B | ✅ |
| DM-DDB-B | Device Management - Dynamic Device Binding - B | ✅ |
| DM-DOB-B | Device Management - Dynamic Object Binding - B | ✅ |
| DM-DCC-B | Device Management - Device Communication Control - B | ✅ |
| GW-VN-B | Gateway - Virtual Network - B | ✅ |

**DS-WP-B, DM-DCC-B and GW-VN-B apply to the GATEWAY only** - the virtual
device answers DS-RP-B, DM-DDB-B and DM-DOB-B on itself, but does not accept
writes or DeviceCommunicationControl. This example does not implement
NM-RC-B or DM-LM-B (physical inter-datalink routing - that is B-RTR's
F-ROUTER/F-MULTIPORT, a different profile), alarming, scheduling, or
trending.

### Services (executed / B-side)

| Service | Notes |
|---------|-------|
| ReadProperty | Answers on BOTH devices (DS-RP-B). |
| WriteProperty | Accepts writes to the gateway's commandable outputs' Present_Value (DS-WP-B). |
| DeviceCommunicationControl | Answers on the gateway only (DM-DCC-B). |
| Who-Is / I-Am | Answers Who-Is with I-Am on BOTH devices; broadcasts I-Am for both at start-up and on the `r` key (DM-DDB-B). |
| Who-Has / I-Have | Answers Who-Has with I-Have (DM-DOB-B). |

### Object types

| Object type | Device | Instance | Name | Access |
|-------------|--------|:--------:|------|--------|
| Device | gateway (389019) | 389019 | Chipkin Example B-GW | - |
| Analog Input | gateway (389019) | 1 | Bronze | read-only |
| Binary Input | gateway (389019) | 1 | Emerald | read-only |
| Multi-State Input | gateway (389019) | 1 | Hot Pink | read-only |
| Analog Output | gateway (389019) | 1 | Chartreuse | writable (commandable) |
| Binary Output | gateway (389019) | 1 | Fuchsia | writable (commandable) |
| Multi-State Output | gateway (389019) | 1 | Indigo | writable (commandable) |
| Network Port | gateway (389019) | 1 | Vermilion | BACnet/IP |
| Network Port | gateway (389019) | 2 | (unnamed) | virtual network 100 (created internally) |
| Device | **virtual (389119)** | 389119 | Chipkin Example B-GW (virtual) | - |
| Analog Input | **virtual (389119)** | 1 | Bronze | read-only |
| Network Port | **virtual (389119)** | 1 | (unnamed) | virtual network 100 (created internally) |

Every required property of every object on both devices, and who answers it,
is in [docs/PICS.md](docs/PICS.md).

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

- `main.cpp` - the example device: the gateway plus the virtual device it represents.
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

### Windows

- **C++ compiler** - install
  [Visual Studio Community](https://visualstudio.microsoft.com/downloads/)
  (free) and select the **"Desktop development with C++"** workload.
- **CMake** - from <https://cmake.org/download/>, or `winget install Kitware.CMake`.

### Linux / macOS

- Debian/Ubuntu: `sudo apt install build-essential cmake git`
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
./build/BACnetExampleBGW --port 47808

# Windows
.\build\Release\BACnetExampleBGW.exe --port 47808
```

Expected output:

```
BACnet B-GW (Gateway) Example - C++ v1.0.0
CAS BACnet Stack version: 6.0.21.0
Common helper (common/) version: 2.5.0
FYI: Listening for BACnet/IP on UDP port 47808 (Network Port 1).
TX 21 bytes to 192.168.3.255:47808 (broadcast) (Network Port 1)
TX 27 bytes to 192.168.3.255:47808 (broadcast) (Network Port 1)
FYI: I-Am broadcast for the gateway (Device 389019) and the virtual device (Device 389119).
FYI: Device 389019 ("Chipkin Example B-GW") ready. Vendor ID 389. Gateways virtual Device 389119 ("Chipkin Example B-GW (virtual)") on virtual network 100. Press 'h' for help.
```

The device listens on UDP **47808** by default. Allow it through your
firewall. To use a different port, pass `--port` (see below).

> **A red `Error:` line about a UUID not being set for a BACnet/SC datalink
> is expected and is not your bug** - the stack starts a BACnet/SC datalink
> this BACnet/IP-only example never configures. [TUTORIAL.md](TUTORIAL.md#troubleshooting)
> explains this and other benign start-up messages.

### Command-line options

| Option | Default | Meaning |
|--------|---------|---------|
| `--port <n>` | `47808` | UDP port for Network Port 1 (Vermilion). |
| `--deviceID <n>` | `389019` | The GATEWAY's BACnet instance number. **Does not move the virtual device**, which is fixed at the gateway's default instance + 100 (`389119`). |
| `--help`, `-h` | - | Show usage and exit. |
| `--version` | - | Print the example, stack, and `common/` helper versions, then exit. |

### Interactive commands

| Key | Action |
|-----|--------|
| `h` | Show the version information and this command list. |
| `q` | Quit. |
| up arrow | Increase the gateway's Analog Input 1 (`Bronze`) by 1.1. |
| down arrow | Decrease the gateway's Analog Input 1 (`Bronze`) by 1.1. |
| `r` | Re-send I-Am for BOTH devices now (`common/` 2.4.0+). |

## Verify

You need a BACnet client, such as the
[CAS BACnet Explorer](https://store.chipkin.com/products/tools/cas-bacnet-explorer).
A **routing-aware** client (or one that lets you address a
device by DNET/MAC directly) is needed to read the virtual device's objects -
see "How the virtual network works" above for why.

**Confirmed working, on the live wire, against this build:**

1. **Who-Is / I-Am, both devices.** An unrestricted Who-Is directly addressed
   to the gateway's UDP endpoint gets I-Am from the gateway (`389019`,
   `Object_Name` = `Chipkin Example B-GW`). A Who-Is sent as an NPDU-level broadcast to
   **DNET 100** (the virtual network) gets I-Am from the virtual device
   (`389119`, vendor `389`, max-APDU `1476`, no segmentation) - both devices'
   unsolicited start-up I-Am broadcasts were also captured (`TX 21 bytes` /
   `TX 27 bytes`, matching the gateway's and the virtual device's I-Am sizes).
2. **ReadProperty, routed to the virtual device (DS-RP-B + GW-VN-B).** An NPDU
   addressed to DNET 100, DADR `05 EF FF` (the virtual device's instance,
   `389119`, encoded as its MAC on the virtual network) successfully reads:
   `Object_Name` = `"Chipkin Example B-GW (virtual)"` on `device,389119`; `Object_Name` =
   `"Bronze"` and `Present_Value` = `19.0` on `analog-input,1` - the virtual
   device's OWN Analog Input, independent of the gateway's.
3. **Separate `Object_List`s.** The gateway's `Object_List` (read unrouted)
   has **9** entries (Device + 3 inputs + 3 outputs + 2 Network Ports). The
   virtual device's `Object_List` (read routed, DNET 100) has **3** entries
   (its own Device object, its Analog Input, and the Network Port the stack
   auto-creates for a virtual device) - visibly different content from the
   gateway's.
4. **WriteProperty (DS-WP-B), gateway only.** WriteProperty Analog Output 1
   `Present_Value` = `25.5` at priority 8; re-read confirms `25.5`;
   `Priority_Array` reads back a full 16-slot array; a NULL write at priority 8
   relinquishes it, and `Present_Value` correctly falls back to
   `Relinquish_Default` = `20.0`.
5. **DeviceCommunicationControl (DM-DCC-B), gateway only.** `disable-initiation`
   accepted (SimpleACK); `enable` accepted (SimpleACK); the device's own log
   printed the expected `DeviceCommunicationControl: disable-initiation (keep
   responding) (indefinitely)` / `... enable (resume communication)
   (indefinitely)` lines for each.
6. **Unrouted access to the virtual device correctly fails.** A plain,
   unrouted ReadProperty naming `device,389119` (no DNET) gets
   `unknown-object` from the gateway - confirming the virtual device's objects
   genuinely live on the virtual network and are not aliased onto the
   gateway's own local object table.

**Not independently re-verified this session** (inherited, unchanged pattern
shared with other examples in the series and already covered by their own
repositories' wire verification): Who-Has/I-Have.

For a property-by-property review against the conformance statement, see
[TUTORIAL.md](TUTORIAL.md).


## The BACnet profile example series

<!-- PROFILE-TABLE:BEGIN (generated from cas-bacnet-stack-examples/docs/profile-table.md - do not edit here) -->
The CAS BACnet Stack supports every standardized device profile in ASHRAE 135-2024 Annex L, and there is one example repository per profile. Pick the profile your device claims, then the language you build in. "Ask" means the example hasn't been built yet for that language - [contact Chipkin](https://www.chipkin.com/contact/) if you need one.

### Controllers (Annex L.4)

| Profile | C++ | Node.js | C# | Rust | Python |
|---|---|---|---|---|---|
| **B-SS** Smart Sensor | [B-SS-CPP](https://github.com/chipkin/BACnetProfileExample-B-SS-CPP) | Ask | Ask | Ask | Ask |
| **B-SA** Smart Actuator | [B-SA-CPP](https://github.com/chipkin/BACnetProfileExample-B-SA-CPP) | Ask | Ask | Ask | Ask |
| **B-ASC** Application Specific Controller | [B-ASC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ASC-CPP) | [B-ASC-Node](https://github.com/chipkin/BACnetProfileExample-B-ASC-Node) | Ask | Ask | Ask |
| **B-AAC** Advanced Application Controller | [B-AAC-CPP](https://github.com/chipkin/BACnetProfileExample-B-AAC-CPP) | Ask | Ask | Ask | Ask |
| **B-BC** Building Controller | [B-BC-CPP](https://github.com/chipkin/BACnetProfileExample-B-BC-CPP) | Ask | Ask | Ask | Ask |

### Life safety controllers (Annex L.5)

| Profile | C++ | Node.js | C# | Rust | Python |
|---|---|---|---|---|---|
| **B-LSC** Life Safety Controller | [B-LSC-CPP](https://github.com/chipkin/BACnetProfileExample-B-LSC-CPP) 🚧 | Ask | Ask | Ask | Ask |
| **B-ALSC** Advanced Life Safety Controller | [B-ALSC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ALSC-CPP) | Ask | Ask | Ask | Ask |

### Access control controllers (Annex L.6)

| Profile | C++ | Node.js | C# | Rust | Python |
|---|---|---|---|---|---|
| **B-ACC** Access Control Controller | [B-ACC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ACC-CPP) | Ask | Ask | Ask | Ask |
| **B-AACC** Advanced Access Control Controller | [B-AACC-CPP](https://github.com/chipkin/BACnetProfileExample-B-AACC-CPP) | Ask | Ask | Ask | Ask |

### Lighting controllers (Annex L.11)

| Profile | C++ | Node.js | C# | Rust | Python |
|---|---|---|---|---|---|
| **B-LD** Lighting Device | [B-LD-CPP](https://github.com/chipkin/BACnetProfileExample-B-LD-CPP) | Ask | Ask | Ask | Ask |
| **B-LS** Lighting Supervisor | [B-LS-CPP](https://github.com/chipkin/BACnetProfileExample-B-LS-CPP) | Ask | Ask | Ask | Ask |

### Elevator controllers (Annex L.13)

| Profile | C++ | Node.js | C# | Rust | Python |
|---|---|---|---|---|---|
| **B-EM** Elevator Monitor | [B-EM-CPP](https://github.com/chipkin/BACnetProfileExample-B-EM-CPP) | Ask | Ask | Ask | Ask |
| **B-EC** Elevator Controller | [B-EC-CPP](https://github.com/chipkin/BACnetProfileExample-B-EC-CPP) | Ask | Ask | Ask | Ask |
| **B-AEC** Advanced Elevator Controller | [B-AEC-CPP](https://github.com/chipkin/BACnetProfileExample-B-AEC-CPP) | Ask | Ask | Ask | Ask |

### Authentication and authorization (Annex L.14)

| Profile | C++ | Node.js | C# | Rust | Python |
|---|---|---|---|---|---|
| **B-AS** Authorization Server | [B-AS-CPP](https://github.com/chipkin/BACnetProfileExample-B-AS-CPP) | Ask | Ask | Ask | Ask |

### Miscellaneous (Annex L.7, combinable with any one family)

| Profile | C++ | Node.js | C# | Rust | Python |
|---|---|---|---|---|---|
| **B-BBMD** Broadcast Management Device | [B-BBMD-CPP](https://github.com/chipkin/BACnetProfileExample-B-BBMD-CPP) | Ask | Ask | Ask | Ask |
| **B-ACDC** Access Control Door Controller | [B-ACDC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ACDC-CPP) | Ask | Ask | Ask | Ask |
| **B-ACCR** Access Control Credential Reader | [B-ACCR-CPP](https://github.com/chipkin/BACnetProfileExample-B-ACCR-CPP) | Ask | Ask | Ask | Ask |
| **B-RTR** Router | [B-RTR-CPP](https://github.com/chipkin/BACnetProfileExample-B-RTR-CPP) | Ask | Ask | Ask | Ask |
| **B-GW** Gateway | [B-GW-CPP](https://github.com/chipkin/BACnetProfileExample-B-GW-CPP) | Ask | Ask | Ask | Ask |
| **B-DAP** Device Address Proxy | [B-DAP-CPP](https://github.com/chipkin/BACnetProfileExample-B-DAP-CPP) | Ask | Ask | Ask | Ask |
| **B-SCHUB** BACnet/SC Hub | [B-SCHUB-CPP](https://github.com/chipkin/BACnetProfileExample-B-SCHUB-CPP) | Ask | Ask | Ask | Ask |
| **B-GENERAL** General device (Annex L.8) | *(satisfied by every example above)* | — | — | — | — |

### Operator interfaces and workstations (Annex L.1–L.3, L.9–L.10, L.12)

Client-side profiles.

| Profile | C++ | Node.js | C# | Rust | Python |
|---|---|---|---|---|---|
| **B-OD** Operator Display | [B-OD-CPP](https://github.com/chipkin/BACnetProfileExample-B-OD-CPP) | Ask | Ask | Ask | Ask |
| **B-OWS** Operator Workstation | planned | — | — | — | — |
| **B-AWS** Advanced Operator Workstation | planned | — | — | — | — |
| **B-XAWS** Extended Advanced Operator Workstation | planned | — | — | — | — |
| **B-LSAP** Life Safety Annunciator Panel | planned | — | — | — | — |
| **B-LSWS** Life Safety Workstation | planned | — | — | — | — |
| **B-ALSWS** Advanced Life Safety Workstation | planned | — | — | — | — |
| **B-ACSD** Access Control Security Display | planned | — | — | — | — |
| **B-ACWS** Access Control Workstation | planned | — | — | — | — |
| **B-AACWS** Advanced Access Control Workstation | planned | — | — | — | — |
| **B-LOD** Lighting Operator Display | planned | — | — | — | — |
| **B-ALWS** Advanced Lighting Workstation | planned | — | — | — | — |
| **B-LCS** Lighting Control Station | planned | — | — | — | — |
| **B-ALCS** Advanced Lighting Control Station | planned | — | — | — | — |
| **B-ED** Elevator Display | planned | — | — | — | — |
| **B-EWS** Elevator Workstation | planned | — | — | — | — |
| **B-AEWS** Advanced Elevator Workstation | planned | — | — | — | — |

🚧 = in progress. "Ask" = not yet built for that language; contact Chipkin if you need it. Profile definitions: ANSI/ASHRAE 135-2024 Annex L. BIBB definitions: Annex K. Get the stack: <https://store.chipkin.com/services/stacks/bacnet-stack>.
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

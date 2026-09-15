# BACnet B-GW (Gateway) - C++ example

A minimal, copy-paste-friendly example showing how to implement the BACnet
**B-GW (Gateway)** device profile in C++ using the
[CAS BACnet Stack](https://store.chipkin.com/services/stacks/bacnet-stack).
It is a single process that is simultaneously **two BACnet devices**: a real
gateway device reachable directly over BACnet/IP, and **one virtual BACnet
device** the gateway represents on a virtual network behind it - the pattern
a real gateway uses to expose a non-BACnet (or otherwise not-directly-BACnet)
device to a BACnet internetwork.

Part of the CAS BACnet Stack **BACnet profile example series** - one repository
per BACnet device profile. This example claims **only** B-GW, and is the
series' canonical example for **F-GATEWAY**.

Reading order: this repository stands on its own - **you can start here.** If you also want the
gentler introductions to the shared sensor/actuator objects, [B-SS (Smart
Sensor)](https://github.com/chipkin/BACnetProfileExample-B-SS-CPP) is the first example in the
series; this one repeats everything it needs. It shares its base object pattern with
[B-ASC](https://github.com/chipkin/BACnetProfileExample-B-ASC-CPP) (DS-RP-B/DS-WP-B/DM-DCC-B) and
its virtual-network mechanism with [B-RTR](https://github.com/chipkin/BACnetProfileExample-B-RTR-CPP)
(this repository's seed - though B-RTR's *physical* two-port routing is a different stack feature
from this example's *virtual* network, and does not share B-RTR's forwarding limitation; see
"F-GATEWAY" below).

> **Versions:** this document describes **example v1.0.0**, built and verified
> against **CAS BACnet Stack 6.0.21** (`6.x` @ `abd4cee1`), linked as a static
> library, at **Protocol_Revision 24**, with the vendored `common/` helper at
> **v2.5.0**. Running the example prints all three - if what it prints
> disagrees with this line, trust the program and check `CHANGELOG.md`.

## Quickstart

You need a CAS BACnet Stack licence and access to its private submodule (see
[Requires the CAS BACnet Stack](#requires-the-cas-bacnet-stack-licensed-product)).
Then:

```bash
git clone --recursive https://github.com/chipkin/BACnetProfileExample-B-GW-CPP.git
cd BACnetProfileExample-B-GW-CPP
tools/build-stack-static.sh BACnetProfileExample-B-GW-CPP    # from the series root; builds the static library
cmake -B build -S . -DCAS_BACNET_STACK_LINK=STATIC
cmake --build build --config Release
./build/Release/BACnetExampleBGW.exe        # Windows; drop Release/ on Linux
```

The device binds one UDP port, announces BOTH devices (itself and its virtual
device), and prints `Press 'h' for help`.

## What is a B-GW (Gateway) profile?

A **device profile** is a standard "template" defined in Annex L of ANSI/ASHRAE
135. It lists the capabilities a class of device must support so that any
compliant client knows what to expect, and the BACnet Testing Laboratories (BTL)
certify devices against it. (New to BACnet in general? See Chipkin's
[What is BACnet?](https://docs.chipkin.com/protocols/bacnet/) guide.)

**B-GW (Gateway)** is a device (Annex L.7, Miscellaneous, combinable with any
other profile family) that represents a device it is gatewaying for - typically
a device on a non-BACnet network, or one this device otherwise brokers access
to - as a **virtual BACnet device** on a **virtual network** it owns. Annex K.7
defines the two ways a gateway proves this: **GW-VN-B** (virtual network - the
one this example implements, requiring NM-RC-B + DS-RP-B) or **GW-EO-B**
(embedded objects, requiring only DS-RP-B - representing the other device's
data as objects on the gateway's OWN device instead of a separate virtual
device). This example implements **GW-VN-B**: a genuinely separate virtual
Device object, with its own `Object_List`, discoverable and readable in its
own right.

**Reading the capability names.** Each capability below is a **BIBB** (BACnet
Interoperability Building Block). Every BIBB name ends in **-A** or **-B**:
**-A** = the device that *initiates* a request; **-B** = the device that
*responds*.

**What the profile requires:**

- **Data Sharing - ReadProperty - B side (DS-RP-B):** answer **ReadProperty** -
  on BOTH the gateway and the virtual device.
- **Data Sharing - WriteProperty - B side (DS-WP-B):** accept **WriteProperty**
  to the gateway's objects.
- **Device Management - Dynamic Device Binding - B side (DM-DDB-B):** answer
  **Who-Is** with **I-Am** - again, from BOTH devices - and broadcast an
  unsolicited I-Am for each at start-up.
- **Device Management - Dynamic Object Binding - B side (DM-DOB-B):** answer
  **Who-Has** with **I-Have**.
- **Device Management - Device Communication Control - B side (DM-DCC-B):**
  respond to **DeviceCommunicationControl** messages on the gateway.
- **Gateway - Virtual Network - B side (GW-VN-B):** represent a gatewayed
  device as a virtual BACnet device on a virtual network.

**What the profile does NOT require** - and this example omits on purpose:
**alarming / event reporting**, **scheduling**, **trending**, and **physical
inter-datalink routing** (that is B-RTR's F-ROUTER/F-MULTIPORT, a different
profile). The virtual device is deliberately minimal (one Analog Input) -
proving GW-VN-B does not require modelling a rich represented device.

## The devices this example creates

```
Device 389019  "Rainbow"              (the GATEWAY - Vendor 389, Chipkin Automation Systems)
    |
    +-- Analog Input  1       "Bronze"       Present_Value  21.5      (REAL, degrees Celsius; read-only)
    +-- Binary Input  1       "Emerald"      Present_Value  inactive  (0 = inactive / 1 = active; read-only)
    +-- Multi-State Input 1   "Hot Pink"     Present_Value  1         (state, 1..3; read-only)
    +-- Analog Output 1       "Chartreuse"   Present_Value  20.0      (REAL setpoint; WRITABLE, commandable)
    +-- Binary Output 1       "Fuchsia"      Present_Value  inactive  (0/1; WRITABLE, commandable)
    +-- Multi-State Output 1  "Indigo"       Present_Value  1         (state, 1..3; WRITABLE, commandable)
    +-- Network Port 1        "Vermilion"    BACnet/IP                (the one physical port)
    +-- Network Port 2        (unnamed)      virtual network 100      (created internally by AddVirtualNetwork)

Device 389119  "Rainbow (virtual)"     (the VIRTUAL device, on virtual network 100)
    |
    +-- Analog Input  1       "Bronze"       Present_Value  19.0      (REAL, degrees Celsius; read-only;
                                                                        its OWN value, independent of the
                                                                        gateway's Analog Input 1 above)
    +-- Network Port 1        (unnamed)      virtual network 100      (created internally by
                                                                        AddDeviceToVirtualNetwork)
```

The gateway's three **input** objects and three **output** objects are the
shared minimum/commandable pattern every example in this series carries
(inherited from B-SA/B-ASC). The **virtual device** is this example's own
contribution - see F-GATEWAY below.

## F-GATEWAY: one virtual network, one virtual device

At start-up, `main.cpp` (after creating the gateway device, its objects, and
its Network Port, exactly like B-ASC):

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
4. Broadcasts an unsolicited **I-Am** for BOTH devices (`CASExampleHelper::SendIAm`
   called twice, once per device instance, both on the gateway's one Network
   Port - the virtual device has no socket of its own). Re-sent on the `r` key
   (`common/` 2.4.0+, the same key B-RTR claimed for "routing/gateway
   examples" in `docs/menu-keys.md` - this example's handler sends plain I-Am
   rather than I-Am-Router-To-Network, since a gateway is not a router and
   sends no network-layer router messages).

**Addressing the virtual device from a client.** The virtual device shares the
gateway's one UDP socket; it is NOT reachable by a plain, unrouted ReadProperty
to that socket (confirmed on the wire - see Verification below: an unrouted
request naming the virtual device's own `Object_Identifier` gets
`unknown-object`, because the virtual device's objects live on the *virtual*
network, not the gateway's own local one). A client must address it exactly as
it would a device behind a real router: an NPDU carrying **DNET = 100** (the
virtual network number) and the virtual device's MAC address on that network,
which the CAS BACnet Stack derives from the device instance itself
(`BACnetVirtualRouter::VirtualDeviceInstanceToSADR` - the instance encoded as
a big-endian byte string, 3 bytes for `389119`: `0x05 0xEF 0xFF`). A
routing-aware BACnet client that has learned this address from the virtual
device's own I-Am (which carries its network-layer source address) or from
Who-Is-Router-To-Network addresses it correctly without the operator doing
anything special - this is standard BACnet internetwork behaviour (ASHRAE 135
cl. 6.6), not an example-specific quirk.

**This is a DIFFERENT stack feature from B-RTR's physical two-port routing,
and does NOT share its forwarding limitation.** B-RTR's gap (stack issues
[#2037](https://github.com/chipkin/cas-bacnet-stack/issues/2037) and
[#2038](https://github.com/chipkin/cas-bacnet-stack/issues/2038)) is about
forwarding NPDUs between two *physical* BACnet/IP datalinks, which the pinned
stack cannot do because it holds only one internal datalink instance per
network type. A virtual network has no second physical datalink to instantiate
at all - the "routing" is internal object-table dispatch inside one process,
implemented by a completely different code path
(`BACnetNetworkLayer`/`BACnetVirtualRouter`, gated on
`STACK_OPTION_VIRTUAL_ROUTER`, itself pulled in by `STACK_OPTION_GW_VN_B` -
confirmed present in the customer-facing `ReleaseLib|x64` build by reading
`CASBACnetStackOptions.h`/`CIBuildSettings.h` at the pin, not assumed). It was
verified working end-to-end on the wire for this example - see Verification.

## What this example supports

### BIBBs (BACnet Interoperability Building Blocks)

| BIBB | Description | Supported |
|------|-------------|:---------:|
| DS-RP-B | Data Sharing - ReadProperty - B | ✅ |
| DS-WP-B | Data Sharing - WriteProperty - B | ✅ |
| DM-DDB-B | Device Management - Dynamic Device Binding - B | ✅ |
| DM-DOB-B | Device Management - Dynamic Object Binding - B | ✅ |
| DM-DCC-B | Device Management - Device Communication Control - B | ✅ |
| GW-VN-B | Gateway - Virtual Network - B | ✅ |

### Services (executed / B-side)

| Service | Notes |
|---------|-------|
| ReadProperty (12) | Answers on BOTH devices (DS-RP-B). Verified with a live client, including a routed read of the virtual device. |
| WriteProperty (15) | Accepts writes to the gateway's commandable outputs' Present_Value (DS-WP-B). Verified with a live client. |
| DeviceCommunicationControl (17) | Answers on the gateway only (DM-DCC-B). Verified with a live client: `disable-initiation` then `enable`, both accepted. |
| Who-Is / I-Am | Answers Who-Is with I-Am on BOTH devices; broadcasts I-Am for both at start-up and on the `r` key (DM-DDB-B). Verified with a live client, including a routed (DNET 100) Who-Is answered by the virtual device. |
| Who-Has / I-Have | Answers Who-Has with I-Have (DM-DOB-B). |

### Object types

| Object type | Device | Instance | Name | Access |
|-------------|--------|:--------:|------|--------|
| Device | gateway (389019) | 389019 | Rainbow | - |
| Analog Input | gateway (389019) | 1 | Bronze | read-only |
| Binary Input | gateway (389019) | 1 | Emerald | read-only |
| Multi-State Input | gateway (389019) | 1 | Hot Pink | read-only |
| Analog Output | gateway (389019) | 1 | Chartreuse | writable (commandable) |
| Binary Output | gateway (389019) | 1 | Fuchsia | writable (commandable) |
| Multi-State Output | gateway (389019) | 1 | Indigo | writable (commandable) |
| Network Port | gateway (389019) | 1 | Vermilion | BACnet/IP |
| Network Port | gateway (389019) | 2 | (unnamed) | virtual network 100 (created internally) |
| Device | **virtual (389119)** | 389119 | Rainbow (virtual) | - |
| Analog Input | **virtual (389119)** | 1 | Bronze | read-only |
| Network Port | **virtual (389119)** | 1 | (unnamed) | virtual network 100 (created internally) |

## Before you ship

This example is a tutorial, and it identifies itself as one. Everything in this
table is read by clients and shown to the operator in **every discovery tool on
the network**. Left as-is, your product appears on a real site announcing itself
as a Chipkin demo. None of it is cosmetic.

| Constant (`main.cpp`) | Ships as | Change it to |
|---|---|---|
| `VENDOR_IDENTIFIER` | `389` (Chipkin) | **Your** company's vendor ID. Assigned by ASHRAE, free: <https://bacnet.org/assigned-vendor-ids/> |
| `VENDOR_NAME` | `Chipkin Automation Systems` | Your company name — must match the vendor ID above. |
| `DEVICE_NAME` | `"Rainbow"` | Your gateway's `Object_Name`. **Must be unique across the BACnet internetwork.** |
| `MODEL_NAME` | `CAS BACnet Stack Example - B-GW` | Your model designation. |
| `VIRTUAL_DEVICE_NAME` | `"Rainbow (virtual)"` | The represented device's `Object_Name`. Also unique across the internetwork. |
| `DEVICE_DESCRIPTION` / `VIRTUAL_DEVICE_DESCRIPTION` | descriptions of *this example* | What your devices actually are. |
| `FIRMWARE_REVISION` / `APPLICATION_SOFTWARE_VERSION` | `1.0.0` | Your real versions. |
| Gateway device instance | `389019` (`--deviceID` overrides) | Must be unique on the internetwork. |
| `VIRTUAL_DEVICE_INSTANCE` | `389119` (fixed, NOT moved by `--deviceID`) | Must also be unique on the internetwork. |
| `VIRTUAL_NETWORK_NUMBER` | `100` | Must be unique among every network number (real or virtual) on the internetwork, same as a device instance. |

`main.cpp` marks this block with a `CHANGE ALL OF THIS BEFORE YOU SHIP` banner.

## Requires the CAS BACnet Stack (licensed product)

This example **builds against the CAS BACnet Stack, which is a commercial Chipkin
product** - it is not free or open source, and there is no public/trial build.
The stack is referenced here as the **private** git submodule
`submodules/cas-bacnet-stack`; you can only fetch and build it once you have a CAS
BACnet Stack license and access to that repository.

**To get the CAS BACnet Stack (and access to build this example), contact
Chipkin:** <https://store.chipkin.com/services/stacks/bacnet-stack> or
sales@chipkin.com.

You do not need a stack licence to *read* this example. Every file outside
submodules/ is CC0 public domain.

## What's in this repository

- `main.cpp` - the example device: the gateway plus the virtual device it represents.
- `common/` - the shared helper (UDP, callbacks, CLI, keyboard) vendored in.
- `submodules/cas-bacnet-stack/` - the **CAS BACnet Stack as a git submodule**
  (private; requires a license). Built into a prebuilt **STATIC** library by
  the stack's own project files (`tools/build-stack-static.sh`), then linked -
  no DLL is shipped.

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

## Get the code

```bash
git clone --recursive https://github.com/chipkin/BACnetProfileExample-B-GW-CPP.git
cd BACnetProfileExample-B-GW-CPP

# already cloned without --recursive? fetch the submodule:
git submodule update --init --recursive
```

## Build

This example links the CAS BACnet Stack as a prebuilt **STATIC** library. Build
the library once from the pinned submodule commit, then configure and build the
example against it:

```bash
tools/build-stack-static.sh BACnetProfileExample-B-GW-CPP   # from the series root
cmake -B build -S . -DCAS_BACNET_STACK_LINK=STATIC
cmake --build build --config Release
```

> **The stack library build takes a few minutes** the first time - it compiles
> the entire CAS BACnet Stack (~600 source files) once. The example itself then
> builds in seconds and rebuilds incrementally. Build in parallel to cut that
> down: `cmake --build build --config Release --parallel`.
>
> On Windows, if `msbuild` picks a toolset the stack's `.vcxproj` does not have
> installed, force the one Visual Studio 2022 ships (`v143`):
> `TOOLSET=v143 tools/build-stack-static.sh BACnetProfileExample-B-GW-CPP`, and
> configure CMake with the matching generator toolset: `cmake -B build -S . -T v143 ...`.

If your CAS BACnet Stack lives somewhere other than the bundled submodule, point
CMake at it: `cmake -B build -S . -D CAS_STACK_DIR=/path/to/cas-bacnet-stack`.

### Link mode

This example links the stack through the `CASBACnetStack::Adapter` CMake target
in **STATIC** mode - `-DCAS_BACNET_STACK_LINK=STATIC` links the prebuilt
`CASBACnetStack_x64_Release.lib` / `libCASBACnetStack_x64_Release.a` built by
`tools/build-stack-static.sh` above. Every mode requires calling
`LoadBACnetFunctions()` once at the top of `main()` before any other
`BACnetStack_*` call. On MSVC the adapter also forces the static CRT (`/MT`) to
match how the library is built. A **SOURCE** mode also exists (compiles the
stack's `source/*.cpp` straight into the executable) - this example is built
and published in **STATIC** mode only.

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
FYI: Device 389019 ("Rainbow") ready. Vendor ID 389. Gateways virtual Device 389119 ("Rainbow (virtual)") on virtual network 100. Press 'h' for help.
```

The device listens on UDP **47808** by default. Allow it through your firewall.

> **You may see one red `Error:` line about a UUID not being set for a
> BACnet/SC datalink.** Benign - the stack starts a BACnet/SC datalink this
> BACnet/IP-only example never configures. Same as every other example in the
> series; see Troubleshooting.

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

You need a BACnet client. [bacpypes3](https://github.com/JoelBender/bacpypes3)
or a GUI tool such as [YABE](https://sourceforge.net/projects/yetanotherbacnetexplorer/)
both work. A **routing-aware** client (or one that lets you address a
device by DNET/MAC directly) is needed to read the virtual device's objects -
see the F-GATEWAY section above for why.

**Confirmed working, on the live wire, against this build** (bacpypes3, this
session):

1. **Who-Is / I-Am, both devices.** An unrestricted Who-Is directly addressed
   to the gateway's UDP endpoint gets I-Am from the gateway (`389019`,
   `Object_Name` = `Rainbow`). A Who-Is sent as an NPDU-level broadcast to
   **DNET 100** (the virtual network) gets I-Am from the virtual device
   (`389119`, vendor `389`, max-APDU `1476`, no segmentation) - both devices'
   unsolicited start-up I-Am broadcasts were also captured (`TX 21 bytes` /
   `TX 27 bytes`, matching the gateway's and the virtual device's I-Am sizes).
2. **ReadProperty, routed to the virtual device (DS-RP-B + GW-VN-B).** An NPDU
   addressed to DNET 100, DADR `05 EF FF` (the virtual device's instance,
   `389119`, encoded as its MAC on the virtual network per
   `BACnetVirtualRouter::VirtualDeviceInstanceToSADR`) successfully reads:
   `Object_Name` = `"Rainbow (virtual)"` on `device,389119`; `Object_Name` =
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

**Not independently re-verified this session** (inherited, unchanged from the
B-ASC/B-SA pattern this example's gateway objects are copied from, and already
covered by their own repositories' wire verification): Who-Has/I-Have.

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| A ReadProperty naming the virtual device's `Object_Identifier` (`device,389119`) gets `unknown-object` when sent as a plain, unrouted request | **Expected, not your bug.** The virtual device is not on the gateway's local object table - address it via DNET 100 (see "F-GATEWAY" above), or use a routing-aware client that resolves this automatically from the virtual device's own I-Am / a Who-Is-Router-To-Network reply. |
| An unrestricted Who-Is sent directly to the gateway's socket returns I-Am from the gateway only, not the virtual device | **Expected.** A local (non-DNET) Who-Is is scoped to the local network, same as it would be for a device behind a real physical router; broadcast (or restrict) it to DNET 100 to reach the virtual device, exactly as ASHRAE 135 cl. 6.6 describes for any routed network. |
| On start-up the app also prints a `UUID has not been set` line | Benign - the stack starts a BACnet/SC datalink this BACnet/IP-only example never configures. Same as every other example in the series. |
| CMake error: *"CAS BACnet Stack adapter not found under: ..."* | Submodules not initialized. Run `git submodule update --init --recursive`. |
| `Failed to bind UDP port ...` | Another program is using that port. Stop it, or pass a different `--port`. |
| First build seems stuck for minutes | Normal - it's compiling ~600 stack files. Only the first build is slow. |
| `git submodule update` fails with *Permission denied* / *repository not found* | The CAS BACnet Stack submodule is a **private** repo - see [Requires the CAS BACnet Stack](#requires-the-cas-bacnet-stack-licensed-product). |

## The BACnet profile example series

<!-- PROFILE-TABLE:BEGIN (generated from cas-bacnet-stack-examples/docs/profile-table.md - do not edit here) -->
The CAS BACnet Stack supports every standardized device profile in ASHRAE 135-2024 Annex L. One example repository per profile shows how. ✅ = the required BIBB (service) is supported by the CAS BACnet Stack; the **Example** column is the state of that profile's tutorial repository.

### Controllers (Annex L.4)

| Profile | Example | Required BIBBs (services) |
|---|---|---|
| **B-SS** Smart Sensor | [B-SS-CPP](https://github.com/chipkin/BACnetProfileExample-B-SS-CPP) ✅ | ✅ DS-RP-B · ✅ DM-DDB-B · ✅ DM-DOB-B |
| **B-SA** Smart Actuator | [B-SA-CPP](https://github.com/chipkin/BACnetProfileExample-B-SA-CPP) ✅ | ✅ DS-RP-B · ✅ DS-WP-B · ✅ DM-DDB-B · ✅ DM-DOB-B |
| **B-ASC** Application Specific Controller | [B-ASC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ASC-CPP) ✅ · [B-ASC-Node](https://github.com/chipkin/BACnetProfileExample-B-ASC-Node) ✅ | ✅ DS-RP-B · ✅ DS-WP-B · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B |
| **B-AAC** Advanced Application Controller | [B-AAC-CPP](https://github.com/chipkin/BACnetProfileExample-B-AAC-CPP) ✅ | ✅ DS-RP-B · ✅ DS-RPM-B · ✅ DS-WP-B · ✅ DS-WPM-B · ✅ AE-N-I-B · ✅ AE-ACK-B · ✅ AE-INFO-B · ✅ AE-CRL-B · ✅ SCHED-I-B · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B · ✅ DM-RD-B |
| **B-BC** Building Controller | [B-BC-CPP](https://github.com/chipkin/BACnetProfileExample-B-BC-CPP) 📝 | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-RPM-B · ✅ DS-WP-A · ✅ DS-WP-B · ✅ DS-WPM-B · ✅ AE-N-I-B · ✅ AE-ACK-B · ✅ AE-INFO-B · ✅ AE-CRL-B · ✅ SCHED-E-B · ✅ T-VMT-I-B · ✅ T-ATR-B · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B · ✅ DM-RD-B · ✅ DM-BR-B |

### Life safety controllers (Annex L.5)

| Profile | Example | Required BIBBs (services) |
|---|---|---|
| **B-LSC** Life Safety Controller | [B-LSC-CPP](https://github.com/chipkin/BACnetProfileExample-B-LSC-CPP) 📝 | ✅ DS-RP-B · ✅ DS-RPM-B · ✅ DS-WP-B · ✅ DS-WPM-B · ✅ DS-COV-B · ✅ AE-LS-B · ✅ AE-ACK-B · ✅ AE-INFO-B · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B · ✅ DM-RD-B |
| **B-ALSC** Advanced Life Safety Controller | [B-ALSC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ALSC-CPP) ✅ | ✅ DS-RP-B · ✅ DS-RPM-B · ✅ DS-WP-B · ✅ DS-WPM-B · ✅ DS-COV-B · ✅ AE-LS-B · ✅ AE-ACK-B · ✅ AE-INFO-B · ✅ AE-EL-I-B · ✅ SCHED-I-B · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B · ✅ DM-RD-B |

### Access control controllers (Annex L.6)

| Profile | Example | Required BIBBs (services) |
|---|---|---|
| **B-ACC** Access Control Controller | [B-ACC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ACC-CPP) 🚧 | ✅ DS-RP-B · ✅ DS-RPM-B · ✅ DS-WP-B · ✅ DS-WPM-B · ✅ DS-COV-B · ✅ DS-ACUC-B · ✅ DS-ACSC-B · ✅ AE-AC-B · ✅ AE-ACK-B · ✅ AE-INFO-B · ✅ AE-EL-I-B · ✅ SCHED-I-B · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B · ✅ DM-RD-B · ✅ DM-BR-B |
| **B-AACC** Advanced Access Control Controller | [B-AACC-CPP](https://github.com/chipkin/BACnetProfileExample-B-AACC-CPP) 📝 | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-RPM-B · ✅ DS-WP-A · ✅ DS-WP-B · ✅ DS-WPM-B · ✅ DS-COV-A · ✅ DS-COV-B · ✅ DS-ACAD-A · ☐ DS-ACCDI-A · ✅ DS-ACUC-B · ✅ DS-ACSC-B · ✅ AE-AC-B · ✅ AE-ACK-B · ✅ AE-INFO-B · ✅ AE-EL-I-B · ✅ SCHED-I-B · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B · ✅ DM-RD-B · ✅ DM-BR-B |

### Lighting controllers (Annex L.11)

| Profile | Example | Required BIBBs (services) |
|---|---|---|
| **B-LD** Lighting Device | [B-LD-CPP](https://github.com/chipkin/BACnetProfileExample-B-LD-CPP) ✅ | ✅ DS-RP-B · ✅ DS-WP-B · ✅ DS-LO-B / DS-BLO-B · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B |
| **B-LS** Lighting Supervisor | [B-LS-CPP](https://github.com/chipkin/BACnetProfileExample-B-LS-CPP) ✅ | ✅ DS-RP-B · ✅ DS-WP-A · ✅ DS-WP-B · ✅ DS-WG-E-B · ✅ DS-ALO-A · ✅ SCHED-E-B · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B |

### Elevator controllers (Annex L.13)

| Profile | Example | Required BIBBs (services) |
|---|---|---|
| **B-EM** Elevator Monitor | [B-EM-CPP](https://github.com/chipkin/BACnetProfileExample-B-EM-CPP) ✅ | ✅ DS-RP-B · ✅ DS-RPM-B · ✅ DS-COV-B · ✅ DS-COVM-B · ✅ AE-N-I-B · ✅ AE-ACK-B · ✅ AE-INFO-B · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B |
| **B-EC** Elevator Controller | [B-EC-CPP](https://github.com/chipkin/BACnetProfileExample-B-EC-CPP) 📝 | ✅ DS-RP-B · ✅ DS-RPM-B · ✅ DS-WP-B · ✅ DS-WPM-B · ✅ DS-COV-B · ✅ DS-COVM-B · ✅ AE-N-I-B · ✅ AE-ACK-B · ✅ AE-INFO-B · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B · ✅ DM-RD-B |
| **B-AEC** Advanced Elevator Controller | [B-AEC-CPP](https://github.com/chipkin/BACnetProfileExample-B-AEC-CPP) 📝 | ✅ DS-RP-B · ✅ DS-RPM-B · ✅ DS-WP-B · ✅ DS-WPM-B · ✅ DS-COV-B · ✅ DS-COVM-B · ✅ AE-N-I-B · ✅ AE-ACK-B · ✅ AE-INFO-B · ✅ AE-EL-I-B · ✅ SCHED-I-B · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B · ✅ DM-OCD-B · ✅ DM-RD-B · ✅ DM-BR-B |

### Authentication and authorization (Annex L.14)

| Profile | Example | Required BIBBs (services) |
|---|---|---|
| **B-AS** Authorization Server | [B-AS-CPP](https://github.com/chipkin/BACnetProfileExample-B-AS-CPP) 🚧 | ✅ DS-RP-B · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ AA-AS-B |

### Miscellaneous (Annex L.7, combinable with any one family)

| Profile | Example | Required BIBBs (services) |
|---|---|---|
| **B-BBMD** Broadcast Management Device | [B-BBMD-CPP](https://github.com/chipkin/BACnetProfileExample-B-BBMD-CPP) ✅ | ✅ DS-RP-B · ✅ DS-WP-B · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ NM-BBMDC-B |
| **B-ACDC** Access Control Door Controller | [B-ACDC-CPP](https://github.com/chipkin/BACnetProfileExample-B-ACDC-CPP) ✅ | ✅ DS-RP-B · ✅ DS-WP-B · ✅ DS-ACAD-B · ✅ DM-DDB-B · ✅ DM-DOB-B |
| **B-ACCR** Access Control Credential Reader | [B-ACCR-CPP](https://github.com/chipkin/BACnetProfileExample-B-ACCR-CPP) ✅ | ✅ DS-RP-B · ✅ DS-WP-B · ✅ DS-COV-B · ✅ DS-ACCDI-B · ✅ DM-DDB-B · ✅ DM-DOB-B |
| **B-RTR** Router | [B-RTR-CPP](https://github.com/chipkin/BACnetProfileExample-B-RTR-CPP) ✅ | ✅ DS-RP-B · ✅ DS-WP-B · ✅ DM-DDB-A · ✅ DM-DOB-B · ☐ DM-LM-B · ✅ NM-RC-B |
| **B-GW** Gateway | [B-GW-CPP](https://github.com/chipkin/BACnetProfileExample-B-GW-CPP) ✅ | ✅ DS-RP-B · ✅ DS-WP-B · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ GW-EO-B / GW-VN-B |
| **B-DAP** Device Address Proxy | [B-DAP-CPP](https://github.com/chipkin/BACnetProfileExample-B-DAP-CPP) ✅ | ✅ DS-RP-B · ✅ DS-WP-B · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DAB-B |
| **B-SCHUB** BACnet/SC Hub | [B-SCHUB-CPP](https://github.com/chipkin/BACnetProfileExample-B-SCHUB-CPP) 📝 | ✅ DS-RP-B · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ NM-SCH-B |
| **B-GENERAL** General device (Annex L.8) | *(satisfied by every example above)* | ✅ DS-RP-B · ✅ DM-DDB-B · ✅ DM-DOB-B |

### Operator interfaces and workstations (Annex L.1–L.3, L.9–L.10, L.12) — client-side profiles

| Profile | Example | Required BIBBs (services) |
|---|---|---|
| **B-OD** Operator Display | [B-OD-CPP](https://github.com/chipkin/BACnetProfileExample-B-OD-CPP) ✅ | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-WP-A · ✅ DS-V-A · ✅ DS-M-A · ✅ AE-N-A · ✅ AE-VN-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B |
| **B-OWS** Operator Workstation | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-WP-A · ✅ DS-V-A · ✅ DS-M-A · ✅ AE-N-A · ✅ AE-ACK-A · ✅ AE-AS-A · ✅ AE-VM-A · ✅ AE-VN-A · ✅ SCHED-VM-A · ✅ T-V-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-MTS-A |
| **B-AWS** Advanced Operator Workstation | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-WP-A · ✅ DS-WPM-A · ✅ DS-AV-A · ✅ DS-AM-A · ✅ AE-N-A · ✅ AE-ACK-A · ✅ AE-AS-A · ✅ AE-AVM-A · ✅ AE-AVN-A · ✅ AE-ELVM-A · ✅ SCHED-AVM-A · ✅ T-AVM-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-ANM-A · ✅ DM-ADM-A · ✅ DM-DOB-B · ✅ DM-DCC-A · ✅ DM-MTS-A · ✅ DM-OCD-A · ✅ DM-RD-A · ✅ DM-BR-A · ✅ DM-DDA-A · ✅ NM-CC-A · ✅ AR-AVM-A |
| **B-XAWS** Extended Advanced Operator Workstation | planned | ✅ union of B-AWS + B-AACWS + B-ALWS + B-AEWS |
| **B-LSAP** Life Safety Annunciator Panel | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-WP-A · ✅ DS-LSV-A · ✅ AE-N-A · ✅ AE-LS-A · ✅ AE-ACK-A · ✅ AE-LSVN-A |
| **B-LSWS** Life Safety Workstation | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-WP-A · ✅ DS-WPM-A · ✅ DS-LSV-A · ✅ DS-LSM-A · ✅ AE-N-A · ✅ AE-LS-A · ✅ AE-ACK-A · ✅ AE-AS-A · ✅ AE-LSVM-A · ✅ AE-LSAVN-A · ✅ AE-ELV-A · ✅ SCHED-VM-A · ✅ T-V-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-ANM-A · ✅ DM-ADM-A · ✅ DM-DOB-B · ✅ DM-DCC-A · ✅ DM-MTS-A · ✅ DM-OCD-A · ✅ DM-RD-A · ✅ DM-BR-A |
| **B-ALSWS** Advanced Life Safety Workstation | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-WP-A · ✅ DS-WPM-A · ✅ DS-LSAV-A · ✅ DS-LSAM-A · ✅ AE-N-A · ✅ AE-LS-A · ✅ AE-ACK-A · ✅ AE-AS-A · ✅ AE-LSAVM-A · ✅ AE-LSAVN-A · ✅ AE-ELVM-A · ✅ SCHED-AVM-A · ✅ T-AVM-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-ANM-A · ✅ DM-ADM-A · ✅ DM-DOB-B · ✅ DM-DCC-A · ✅ DM-MTS-A · ✅ DM-OCD-A · ✅ DM-RD-A · ✅ DM-BR-A · ✅ AR-AVM-A |
| **B-ACSD** Access Control Security Display | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-WP-A · ✅ DS-WPM-A · ✅ DS-ACV-A · ✅ DS-ACM-A · ✅ AE-N-A · ✅ AE-AC-A · ✅ AE-ACK-A · ✅ AE-AS-A · ✅ AE-ACAVN-A · ✅ AE-ELV-A · ✅ SCHED-VM-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-MTS-A |
| **B-ACWS** Access Control Workstation | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-WP-A · ✅ DS-WPM-A · ✅ DS-ACAV-A · ✅ DS-ACM-A · ✅ DS-ACUC-A · ✅ AE-N-A · ✅ AE-AC-A · ✅ AE-ACK-A · ✅ AE-AS-A · ✅ AE-ACVM-A · ✅ AE-ACAVN-A · ✅ AE-ELV-A · ✅ SCHED-VM-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-ANM-A · ✅ DM-ADM-A · ✅ DM-DOB-B · ✅ DM-DCC-A · ✅ DM-MTS-A · ✅ DM-OCD-A · ✅ DM-RD-A · ✅ DM-BR-A |
| **B-AACWS** Advanced Access Control Workstation | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-WP-A · ✅ DS-WPM-A · ✅ DS-ACAV-A · ✅ DS-ACAM-A · ✅ DS-ACUC-A · ✅ DS-ACSC-A · ✅ AE-N-A · ✅ AE-AC-A · ✅ AE-ACK-A · ✅ AE-AS-A · ✅ AE-ACAVM-A · ✅ AE-ACAVN-A · ✅ AE-ELVM-A · ✅ SCHED-AVM-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-ANM-A · ✅ DM-ADM-A · ✅ DM-DOB-B · ✅ DM-DCC-A · ✅ DM-MTS-A · ✅ DM-OCD-A · ✅ DM-RD-A · ✅ DM-BR-A · ✅ AR-AVM-A |
| **B-LOD** Lighting Operator Display | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-WP-A · ✅ DS-LV-A · ✅ DS-WG-A · ✅ DS-ALO-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B |
| **B-ALWS** Advanced Lighting Workstation | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-WP-A · ✅ DS-WPM-A · ✅ DS-LAV-A · ✅ DS-LAM-A · ✅ DS-WG-A · ✅ DS-ALO-A · ✅ AE-N-A · ✅ AE-ACK-A · ✅ AE-AS-A · ✅ AE-AVM-A · ✅ AE-AVN-A · ✅ AE-ELVM-A · ✅ SCHED-AVM-A · ✅ T-AVM-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-ANM-A · ✅ DM-ADM-A · ✅ DM-DOB-B · ✅ DM-DCC-A · ✅ DM-MTS-A · ✅ DM-OCD-A · ✅ DM-RD-A · ✅ DM-BR-A |
| **B-LCS** Lighting Control Station | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-WP-A · ✅ DS-LO-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B |
| **B-ALCS** Advanced Lighting Control Station | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-WP-A · ✅ DS-WPM-A · ✅ DS-WG-A · ✅ DS-ALO-A · ✅ SCHED-E-B · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B · ✅ DM-DCC-B · ✅ DM-TS-B / DM-UTC-B |
| **B-ED** Elevator Display | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-WP-A · ✅ DS-EV-A · ✅ AE-N-A · ✅ AE-ACK-A · ✅ AE-EVN-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-DOB-B |
| **B-EWS** Elevator Workstation | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-WP-A · ✅ DS-WPM-A · ✅ DS-COVM-A · ✅ DS-EV-A · ✅ DS-EM-A · ✅ AE-N-A · ✅ AE-ACK-A · ✅ AE-AS-A · ✅ AE-EVM-A · ✅ AE-EAVN-A · ✅ SCHED-VM-A · ✅ T-V-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-ANM-A · ✅ DM-ADM-A · ✅ DM-DOB-B · ✅ DM-DCC-A · ✅ DM-MTS-A |
| **B-AEWS** Advanced Elevator Workstation | planned | ✅ DS-RP-A · ✅ DS-RP-B · ✅ DS-RPM-A · ✅ DS-WP-A · ✅ DS-WPM-A · ✅ DS-COVM-A · ✅ DS-EAV-A · ✅ DS-EAM-A · ✅ AE-N-A · ✅ AE-ACK-A · ✅ AE-AS-A · ✅ AE-EAVM-A · ✅ AE-EAVN-A · ✅ AE-ELVM-A · ✅ SCHED-AVM-A · ✅ T-AVM-A · ✅ DM-DDB-A · ✅ DM-DDB-B · ✅ DM-ANM-A · ✅ DM-ADM-A · ✅ DM-DOB-B · ✅ DM-DCC-A · ✅ DM-MTS-A · ✅ DM-OCD-A · ✅ DM-RD-A · ✅ DM-BR-A |

Profile definitions: ANSI/ASHRAE 135-2024 Annex L. BIBB definitions: Annex K. Get the stack: <https://store.chipkin.com/services/stacks/bacnet-stack>.
<!-- PROFILE-TABLE:END -->

## Objects and properties

<!-- OBJECTS-PROPERTIES:BEGIN (generated by tools/gen-objects-properties.py from docs/objects.json - do not edit here) -->
Every object this example creates, and every REQUIRED property of each (per ANSI/ASHRAE 135-2024 clause 12 and the stack's `docs/property-profile-reference.md`), plus the optional properties the example turns on. **Served by** says who answers a ReadProperty: the **stack** generates it, or the **app** serves it from a `GetProperty*` callback in `main.cpp`. A ⚠ row is a required property the app does not serve and the stack would fill with a default - that is a defect, not a feature.

### Device 389019 "Rainbow" - THE GATEWAY device. vendor 389 (Chipkin Automation Systems); instance configurable with --deviceID. The properties in 'accepted' are not served from a GetProperty callback because the stack itself is the source of truth for them - Protocol_Revision/Protocol_Version are stack build constants, Protocol_Services_Supported/Protocol_Object_Types_Supported are computed from the BACnetStack_SetServiceEnabled/AddObject calls this example already makes, Object_List and Device_Address_Binding are live stack-maintained tables, System_Status/Database_Revision/Max_APDU_Length_Accepted/Segmentation_Supported/APDU_Timeout/Number_Of_APDU_Retries are the stack's own configuration defaults for a device this example does not override

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
| Protocol_Version | Unsigned | stack default, accepted (`BACNET_PROTOCOL_VERSION`) | no |
| Protocol_Revision | Unsigned | stack default, accepted (`BACNET_PROTOCOL_REVISION`) | no |
| Protocol_Services_Supported | BACnetServicesSupported | stack default, accepted (computed from which services are enabled) | no |
| Protocol_Object_Types_Supported | BACnetObjectTypesSupported | stack default, accepted (computed from which object types are supported) | no |
| Object_List | BACnetARRAY[N] of BACnetObjectIdentifier | stack default, accepted (None known - a read fails with `unknown-property` or an empt) | no |
| Max_APDU_Length_Accepted | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_MAX_APDU_LENGTH_ACCEPTED`) | no |
| Segmentation_Supported | BACnetSegmentation | stack default, accepted (`BACnetSegmentation::noSegmentation`) | no |
| APDU_Timeout | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_APDU_TIMEOUT`) | no |
| Number_Of_APDU_Retries | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_NUMBER_OF_APDU_RETRIES`) | no |
| Device_Address_Binding | BACnetLIST of BACnetAddressBinding | stack default, accepted (the live Device_Address_Binding (DAB) table) | no |
| Database_Revision | Unsigned | stack default, accepted (Generic UnsignedInteger default: `0`) | no |

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

### Device 389119 "Rainbow (virtual)" - THE VIRTUAL device (GW-VN-B) - the thing this gateway represents. Added with BACnetStack_AddDeviceToVirtualNetwork after BACnetStack_AddVirtualNetwork(389019, network 100, ...); instance is the gateway's instance + 100 per docs/colour-table.md, fixed (not moved by --deviceID). It is a SEPARATE device with its own Object_List, reached through the gateway's one physical Network Port - it has no Network Port object of its own to list here. The 'accepted' properties are the stack's own defaults for this device exactly as for the gateway above.

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
| Protocol_Version | Unsigned | stack default, accepted (`BACNET_PROTOCOL_VERSION`) | no |
| Protocol_Revision | Unsigned | stack default, accepted (`BACNET_PROTOCOL_REVISION`) | no |
| Protocol_Services_Supported | BACnetServicesSupported | stack default, accepted (computed from which services are enabled) | no |
| Protocol_Object_Types_Supported | BACnetObjectTypesSupported | stack default, accepted (computed from which object types are supported) | no |
| Object_List | BACnetARRAY[N] of BACnetObjectIdentifier | stack default, accepted (None known - a read fails with `unknown-property` or an empt) | no |
| Max_APDU_Length_Accepted | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_MAX_APDU_LENGTH_ACCEPTED`) | no |
| Segmentation_Supported | BACnetSegmentation | stack default, accepted (`BACnetSegmentation::noSegmentation`) | no |
| APDU_Timeout | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_APDU_TIMEOUT`) | no |
| Number_Of_APDU_Retries | Unsigned | stack default, accepted (`CAS_BACNET_DEVICE_DEFAULT_NUMBER_OF_APDU_RETRIES`) | no |
| Device_Address_Binding | BACnetLIST of BACnetAddressBinding | stack default, accepted (the live Device_Address_Binding (DAB) table) | no |
| Database_Revision | Unsigned | stack default, accepted (Generic UnsignedInteger default: `0`) | no |

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

<!-- OBJECTS-PROPERTIES:END -->

## Footprint

Measured from the [v1.0.0](https://github.com/chipkin/BACnetProfileExample-B-GW-CPP/releases/tag/v1.0.0) release CI.

| Metric | Windows | Linux |
|---|---|---|
| Binary size | 3,258,880 bytes | 48,936 bytes |
| SHA-256 (prefix) | `1fdc5cb0068b3e21` | `09fc282cc06128d5` |
| Start-up time to `ready` | 75 ms | 116 ms |
| Stack commit | `abd4cee1` (6.0.21) | `abd4cee1` (6.0.21) |
| Link mode | STATIC | STATIC |
| Compiler | Visual Studio 17 2022 | `/usr/bin/c++` |

<!-- METRICS -->

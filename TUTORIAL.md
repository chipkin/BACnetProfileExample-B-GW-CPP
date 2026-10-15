# Tutorial - extending and reviewing the B-GW example

[README.md](README.md) says what this example *is*. This document is the
*how*: how to put your own foreign-bus driver behind it, how to add points,
who serves which property, how to review the result, and what goes wrong when
you get it subtly right.

- [The shape of the gateway](#the-shape-of-the-gateway)
- [Putting your own driver behind the seam](#putting-your-own-driver-behind-the-seam)
- [Adding a point to the register map](#adding-a-point-to-the-register-map)
- [When a foreign device goes offline](#when-a-foreign-device-goes-offline)
- [Dispatch on deviceInstance first](#dispatch-on-deviceinstance-first)
- [What each object type needs you to serve](#what-each-object-type-needs-you-to-serve)
- [Who serves what: the application or the stack?](#who-serves-what-the-application-or-the-stack)
- [Reviewing your device](#reviewing-your-device)
- [Troubleshooting](#troubleshooting)

## The shape of the gateway

```
 BACnet client  --ReadProperty/WriteProperty-->  CAS BACnet Stack
                                                     | callbacks (GetProperty*/SetProperty*)
                                                     v
                                         main.cpp: the CACHE (GatewayedSlave)
                                           ^ poll results          | winning values
                                           |                       v
                                  ServicePoll()           ServiceWriteThrough()
                                           \                       /
                                            ForeignBus.h  <-- THE SEAM
                                                     |
                           simulation/SimulatedForeignBus.cpp   (or YOUR driver)
```

Two rules hold the design together:

1. **BACnet is answered from the cache, never from the bus.** A callback that
   waited on a slow foreign device would hold the stack's one thread and
   time out the client (Annex H.2.1.4). `ServicePoll()` keeps the cache
   fresh on each slave's own poll cycle; the callbacks only read it.
2. **The bus is asynchronous.** `ForeignBus::StartRead` / `StartWrite` return
   at once with a transaction id; `CheckTransaction` reports `Pending`,
   `Done` or `Timeout` on a later pass. Every pass of the main loop calls
   `BACnetStack_Tick()` and `ServiceForeignBus()` and neither ever blocks.

## Putting your own driver behind the seam

`simulation/` is **Simulation** - pretend field devices so the example has
something to gateway. A real product replaces it:

1. Write `MyModbusTcpBus.cpp` (or RTU, or your PLC's protocol...) that
   implements every function in [ForeignBus.h](ForeignBus.h):
   - `Start()` - open the serial port / connect the TCP sockets.
   - `Service(now)` - send queued requests, read answers that have arrived,
     expire requests older than your response timeout. Non-blocking: use
     non-blocking sockets or a worker thread, never a blocking `recv`.
   - `StartRead` / `StartWrite` - queue a request, return an id (never 0).
   - `CheckTransaction` - report it; forget the id once `Done` or `Timeout`.
2. In `CMakeLists.txt`, replace `SIMULATION_SOURCES` with your file and delete
   the `simulation/` folder - including `SimulationControl.cpp`, the Wire
   Tests' back door, which has no place in a product.
3. Remove the three `SimulatedForeignBus::` uses in `main.cpp`: the register
   addresses (move them into your own register-map header), the slave names in
   the Description strings, and the `k` / up / down keys, which reach inside
   the simulation.

`main.cpp` does not otherwise change: the cache, the poll scheduler, the
write-through and every BACnet callback stay as they are. If your protocol
reads several registers in one request (Modbus function 3 reading a block),
make one transaction return several values - widen `CheckTransaction`'s
out-parameter - rather than issuing one request per point.

## Adding a point to the register map

Say each slave also has a discrete input (on/off status) you want as a Binary
Input. Read the whole recipe first - the silent steps are at the end.

1. **The foreign side**: its address in the register map, and (for the
   simulation) a value in `SimulatedSlave` and a `FindPoint` case.
2. **The cache**: a field in `GatewayedSlave` (`bool runStatus;`).
3. **Polling**: read it in `ServicePoll` - a second transaction, or one block
   read (see above). Only `Done` updates the cache; a `Timeout` counts towards
   `OFFLINE_AFTER_FAILED_POLLS` exactly like the existing poll.
4. **The mapping**: a `PointKind::RunStatus`, and an `OBJECT_TYPE_BINARY_INPUT`
   branch in `FindMappedPoint`.
5. **The object**: `OBJECT_TYPE_BINARY_INPUT` in `AddMappedObjects`'
   `objectTypes[]`, skipped by the commandable block like the Analog Input.
   Reliability and Description are enabled for it by the same loop.
6. **The callbacks** - `Present_Value` (Enumerated) and `Polarity` in
   `GetPropertyEnumerated`, a colour name in `MappedObjectName` (Binary Input
   is "Emerald", per the series colour table), a register in
   `MappedObjectDescription`.
7. **DO NOT SKIP: `Reliability`.** The existing `Reliability` branch answers
   any mapped point, so it covers the new one *if* `FindMappedPoint` finds it.
   If it does not, the stack's default is `no-fault-detected` - the new object
   stays healthy while its slave is dead, and nothing on the wire says so.
8. **DO NOT SKIP: a Wire Test.** Add the check to `tests/mapping_test.py`
   (it follows the register) and to `tests/failure_mode_test.py`'s
   `OBJECT_TYPES` (it faults with its slave), then remove the feature and
   confirm the check fails.

> **Why a forgotten step is SILENT.** Falling through a callback does not
> reliably produce an error: the stack errors only for the few properties it
> refuses to invent (`Present_Value`, `Relinquish_Default` on a commandable
> object). For the rest it substitutes a default - `Object_Name` reads
> **`"undefined"`**, `Units` reads **`no-units`**, `Reliability` reads
> **`no-fault-detected`** - while the object's `Property_List` still
> advertises the property. Nothing on the wire says you forgot anything. The
> `uint32_t* errorCode` out-parameter does not save you either: setting it on
> every catch-all breaks the required properties the stack is meant to answer
> (`Max_APDU_Length_Accepted`, `APDU_Timeout`, ...). Set it only where this
> device knows the request is wrong (`SetPropertyEnumerated`'s out-of-range
> value, `DeviceCommunicationControl`'s rejects).

## When a foreign device goes offline

`ServicePoll` counts unanswered polls; at `OFFLINE_AFTER_FAILED_POLLS` (2) in
a row `SlaveWentOffline` runs, and the first answered poll after that runs
`SlaveCameBack`. What they do depends on the mode, because Annex H asks
different things of the two kinds of gateway:

| | `--mode eo` - Annex H.2.3.2 | `--mode vn` - Annex H.2.2.1 |
|---|---|---|
| What the clause asks | Show it in Reliability and the FAULT flag; reads still return values, "not errors". | Send no I-Am / I-Have for it, and "remain silent" to requests directed to it. |
| What this example does | Reliability = `communication-failure` (the stack sets FAULT from it); reads answered from the cache. | Who-Is and Who-Has switched off for that virtual device (no I-Am / I-Have); reads still answered, from the cache, with Reliability `communication-failure`. |
| Gap | - | **Silence is not possible today** - no customer export makes a virtual device ignore a confirmed request. See README "Known gaps". |

In both modes the outputs' pending writes are marked unwritten when a slave
goes offline, so the current winning value is written again as soon as it is
back (writes made while it was offline included) - Annex H.2.1.1's write
caching, carried across the outage. A write the slave does not acknowledge
(a timeout - or, from a real driver, a refusal such as a Modbus exception)
is retried one poll cycle later, not on every pass, and logged.

Tune `OFFLINE_AFTER_FAILED_POLLS` to your bus: one lost frame on a noisy RS-485
line should not flap a whole device into fault, but an operator should not
wait a minute to see a dead one either.

## Dispatch on deviceInstance first

In `--mode vn` this process answers for **four** devices - the gateway and
three virtual devices - and every virtual device has an "Analog Input 1
(Bronze)" at the same object type and instance. Only `deviceInstance` tells
them apart. `FindMappedPoint()` is the ONE place that turns (device, type,
instance) into a slave and a point kind, in both modes; every callback goes
through it, so a new point added there works in both modes at once. A callback
that tested `objectInstance` alone would answer the Boiler's value for all
three virtual devices - a defect no single-device test catches. The vn half of
`tests/mapping_test.py` reads two virtual devices for exactly this reason.

## What each object type needs you to serve

The application serves every REQUIRED property the stack does not generate:

| Object type | You serve | Plus (this gateway) |
|---|---|---|
| Analog Input | `Present_Value` (Real), `Object_Name`, `Units`, `Out_Of_Service` | `Reliability`, `Description` |
| Analog Output (commandable) | `Object_Name`, `Units`, `Relinquish_Default`, `Out_Of_Service`; Priority_Array slots | `Reliability`, `Description` |
| Binary Output (commandable) | `Object_Name`, `Polarity`, `Relinquish_Default`, `Out_Of_Service`; Priority_Array slots | `Reliability`, `Description` |
| Device (gateway, and each virtual device) | `Object_Name`, `Vendor_Name`, `Vendor_Identifier`, `Model_Name`, `Firmware_Revision`, `Application_Software_Version` | `Description` |

## Who serves what: the application or the stack?

For a mapped Analog Output - the most involved object, since the stack
resolves its `Present_Value` from slots the application only stores:

| Property | Served by | How |
|---|---|---|
| `Object_Identifier`, `Object_Type`, `Property_List` | **stack** | generated from the object you added |
| `Status_Flags` | **stack**, from your data | assembled from `Reliability` (FAULT), `Event_State` and `Out_Of_Service`, which it reads back through your callbacks |
| `Reliability` | **you** | `GetPropertyEnumerated` - `no-fault-detected` or `communication-failure` from the slave's `online` |
| `Event_State` | **stack** | no intrinsic alarming here; reads `normal` (the datatype default) |
| `Out_Of_Service` | **you** | `GetPropertyBool` |
| `Present_Value` | **stack**, from your data | the highest-priority non-null slot, or `Relinquish_Default` |
| `Priority_Array` | **stack**, from your data | `GetPropertyBool` says whether slot *i* is NULL, `GetPropertyReal` gives its value |
| `Current_Command_Priority` | **stack** | from which slot wins |
| `Relinquish_Default`, `Units` | **you** | `GetPropertyReal`, `GetPropertyEnumerated` |
| `Object_Name`, `Description` | **you** | `GetPropertyCharString` |

The gateway resolves the same winner itself (`WinningValue()`) - not to
answer BACnet, but to know what to write to the slave.

Every object in both modes is in [docs/PICS.md](docs/PICS.md).

## Reviewing your device

1. **Run the Wire Tests** (README "Wire Tests") in both modes. They are the
   only check here that notices a feature *disappearing*: each one was
   verified to fail with its feature removed ([tests/README.md](tests/README.md)).
   A new feature gets a new check, verified the same way - a check that has
   never failed proves nothing.
2. Regenerate [docs/PICS.md](docs/PICS.md) after editing `docs/objects.json`
   (below). A ⚠ row is a required property nothing serves.
3. Read every property of every object with a BACnet client and compare it to
   the PICS. `"undefined"`, `no-units` and `0` are the shapes a missed
   callback takes. In `--mode vn`, read through the gateway (DNET 100).
4. Pull a slave (`k`) and re-read: every object mapped to it must fault, and
   no other object may.

### Keeping the PICS honest

`docs/PICS.md` is partly generated. `docs/objects.json` describes each object
and who serves which property; the series tool regenerates the object tables
from it plus the stack's `docs/property-profile-reference.md` at the pinned
commit:

```bash
python tools/gen-objects-properties.py BACnetProfileExample-B-GW-CPP            # rewrite
python tools/gen-objects-properties.py BACnetProfileExample-B-GW-CPP --check    # fail if stale
```

(That tool lives in the example-series repository, not in this one. If you
only have this repository, edit the generated block by hand and keep it
matching the callbacks in `main.cpp`.) The `app` list is what the callbacks
serve; `stack` is a device-wide fact the stack computes; `accepted` is a
required property deliberately left to the stack's default, justified in the
object's `note`.

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| Analog Input reads `0.0` for the first half second | The cache is empty until the first poll answers. Your product may prefer to start each slave `online = false` (fault until proven otherwise). |
| Every object of one slave reads Reliability `communication-failure` | The gateway's polls to it time out: the slave is offline, or (with a real driver) the address, unit id or timeout is wrong. The console says `GATEWAY: slave n ... stopped answering`. |
| A write is acknowledged but the foreign register does not change | The slave is offline - the write is cached and lands when it is back (`GATEWAY: wrote ...`). Or a higher priority slot is still set: read `Priority_Array`. |
| `--mode vn`: a ReadProperty naming `device,389119` gets `unknown-object` | **Expected.** A virtual device lives on network 100; address it with DNET 100 / DADR (README "Reaching a virtual device"). |
| `--mode vn`: Who-Is sent to the gateway's socket gets only the gateway's I-Am | **Expected.** A local Who-Is is scoped to the local network; send it to DNET 100 or 65535 to reach the virtual devices. |
| `--mode vn`: a virtual device stopped answering Who-Is | Its slave is offline (Annex H.2.2.1). It answers again once the slave does. |
| On start-up one red `Error:` line about a UUID not being set | **Expected.** The stack starts a BACnet/SC datalink this BACnet/IP-only example never configures. |
| `SIMULATION: test control channel listening ...` | `BGW_SIMULATION_CONTROL_PORT` is set in your environment - that is the Wire Tests' back door. Unset it. |
| CMake error: *"CAS BACnet Stack adapter not found under: ..."* | Submodules not initialized. Run `git submodule update --init --recursive` (or pass `-D CAS_STACK_DIR=...`). |
| Windows: *"No CMAKE_CXX_COMPILER could be found"* | Install Visual Studio with the "Desktop development with C++" workload, then re-run from a fresh terminal. |
| First build seems stuck for minutes | Normal - it's compiling ~600 stack files. Only the first build is slow. |
| App prints *"Failed to bind UDP port 47808"* | Another BACnet program is already using 47808. Stop it, or run with `--port <n>`. |

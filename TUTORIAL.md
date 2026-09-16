# Tutorial - extending and reviewing the B-GW example

[README.md](README.md) says what this example *is*. This document is the
*how*: how to extend it into your own gateway, who serves which property on
which device, how to review the result for conformance, and what goes wrong
when you get it subtly right.

Read this once before you start changing `main.cpp`. The most expensive
mistake in this example is silent, and it is specific to a device that
answers for TWO devices from one process - see
[Dispatch on deviceInstance first](#dispatch-on-deviceinstance-first).

- [Extending the example](#extending-the-example)
- [Dispatch on deviceInstance first](#dispatch-on-deviceinstance-first)
- [What each object type needs you to serve](#what-each-object-type-needs-you-to-serve)
- [Who serves what: the application or the stack?](#who-serves-what-the-application-or-the-stack)
- [Reviewing your device](#reviewing-your-device)
- [Troubleshooting](#troubleshooting)

## Extending the example

The example is intentionally small so it's easy to change.

**Change a sensor's value or name** - edit the constants / callbacks in
`main.cpp` (e.g. `g_analogInput1Value` for the gateway's Analog Input, or
`g_virtualAnalogInput1Value` for the virtual device's - they are independent,
see below).

**Change the device identity before you ship** - vendor ID, vendor name,
model names, descriptions, firmware revision and BOTH devices' names are all
in the `CHANGE ALL OF THIS BEFORE YOU SHIP` block at the top of `main.cpp`,
with a per-field note on each saying what to change it to. That block is the
authoritative checklist; it is in the source rather than here so it cannot be
skipped by someone who only reads the code. It covers the gateway's
`VIRTUAL_DEVICE_INSTANCE` and `VIRTUAL_NETWORK_NUMBER` too - both must stay
unique across the internetwork, exactly like a device instance.

### Add a second commandable output

Read this whole recipe before starting - the last two steps are the ones that
are easy to miss and the ones BTL will fail you for.

> **Why forgetting a step is SILENT, not loud.** Most of the `GetProperty*`
> callbacks match on **deviceInstance**, then object type **and** instance
> (`objectInstance == ANALOG_OUTPUT_INSTANCE`), so a new instance falls
> through every one of them. `GetPropertyBool`'s `Out_Of_Service` case is the
> exception: it matches on type only, so it works for a new instance for
> free.
>
> Here is the part that matters, and it is the opposite of what most people
> assume: falling through a callback does **not** reliably produce an error.
> The stack errors only for the few properties it refuses to invent -
> `Present_Value` on a non-commandable object, `Number_Of_States`,
> `Relinquish_Default` on a commandable object with no `GetCommandable()`
> match, `Local_Date`, `Local_Time`, and a Network Port's `APDU_Length`. For
> everything else it **silently substitutes a default**:
>
> | Property | If you forget to serve it | Loud? |
> |---|---|:--:|
> | `Present_Value` (non-commandable) | Error (`read-access-denied`) | yes |
> | `Present_Value` (commandable, no `GetCommandable()` entry) | Error | yes |
> | `Object_Name` | reads back as the string **`"undefined"`** | **no** |
> | `Units` | reads back as **`no-units` (95)** | **no** |
> | `Relinquish_Default` (no `GetCommandable()` entry) | Error | yes |
> | `Out_Of_Service` | served on type alone - works by accident | n/a |
>
> **Doesn't the `errorCode` out-parameter fix this?** Only if you use it, and
> only where it is right to. Each `GetProperty*`/`SetProperty*` callback ends
> with a `uint32_t* errorCode` that the stack presets to `success` and reads
> only when you return `false`, so you *can* turn any decline into a chosen
> BACnet error. But ending every callback with
> `*errorCode = unknown-property` breaks the device: the stack's
> decline-and-fabricate path is what answers required properties an
> application is not expected to serve - the Device's
> `Max_APDU_Length_Accepted`, `APDU_Timeout` and `Number_Of_APDU_Retries`
> among them. Name an error on the catch-all and those start failing instead
> of answering. Set `errorCode` only where *this device* knows the read is
> wrong: `main.cpp` does it for `State_Text` with an out-of-range array
> index, for `DeviceCommunicationControl`'s reject paths, and for a
> `SetProperty*` value out of range. The table above is still how the
> fall-through behaves for everything else, and the diff below is still what
> catches a missed step.
>
> It is worse than "wrong value": the object's `Property_List` **still
> advertises `Units` (117)**. So the object actively claims to have the
> property, and then answers with a default. Nothing on the wire says you
> forgot anything.

```cpp
// 1) a new instance number (in section 1, with the other output instances).
//    Naming: a second object of a type is "<Colour> 2" - so Analog Output 2
//    is "Chartreuse 2", NOT a new colour. Each object TYPE owns one colour
//    series-wide.
static const uint32_t ANALOG_OUTPUT_2_INSTANCE = 2;   // "Chartreuse 2"

// 2) a second Commandable and a GetCommandable() case for it. GetCommandable
//    is the ONE place that decides which struct a (deviceInstance, type,
//    instance) triple maps to - every Get/SetProperty* callback for a
//    commandable property goes through it. Miss this and the object silently
//    behaves as if it were never made commandable: WriteProperty gets
//    read-access-denied instead of succeeding, no ⚠ in the PICS, and nothing
//    in Property_List looks wrong until a client actually tries to write it.
static Commandable g_analogOutput2 = { { false }, { 0 }, 20.0 };
//   in GetCommandable(): add
//   if (objectType == OBJECT_TYPE_ANALOG_OUTPUT && objectInstance == ANALOG_OUTPUT_2_INSTANCE) {
//       return &g_analogOutput2;
//   }

// 3) add the object (in main, next to the other BACnetStack_AddObject calls
//    for the GATEWAY - the virtual device has no outputs).
if (!BACnetStack_AddObject(g_deviceInstance, OBJECT_TYPE_ANALOG_OUTPUT, ANALOG_OUTPUT_2_INSTANCE)) {
    printf("Error: Failed to add Analog Output 2 (Chartreuse 2).\n");
    return 1;
}

// 4) DO NOT SKIP: enable Priority_Array/Relinquish_Default and make
//    Present_Value writable, exactly like the loop over `outputs[]` already
//    does for the other three outputs - add this instance to that array
//    rather than hand-rolling a fourth copy of the three
//    BACnetStack_SetPropertyEnabled/SetPropertyWritable calls.

// 5) DO NOT SKIP: serve Units, in GetPropertyEnumerated - the existing check
//    reads `objectInstance == ANALOG_OUTPUT_INSTANCE`, instance 1 - so
//    without this, reading Analog Output 2's Units returns `no-units` and
//    the object is NON-CONFORMANT. It will still accept WriteProperty and
//    its Present_Value will read back perfectly, so the device looks healthy
//    right up until BTL certification.
```

Then re-run the README's Verify steps **against Analog Output 2**, not just
Analog Output 1 - read every required property AND write to it, then diff
both against Analog Output 1. Any property that comes back `"undefined"`,
`no-units`, or rejects a write that instance 1 accepts is a step you missed.
Because the failure is silent (see the table above), this diff is the only
thing that catches it.

## Dispatch on deviceInstance first

This example answers for **two** devices - the gateway (`g_deviceInstance`)
and the virtual device (`VIRTUAL_DEVICE_INSTANCE`) - from one process, and
both have an "Analog Input 1 (Bronze)" at the exact same object type and
instance number. Object type + instance alone cannot tell them apart; only
`deviceInstance` can. Every `Get/SetProperty*` callback in `main.cpp`
dispatches on `deviceInstance` **first**, before it looks at object type or
instance - see the `if (deviceInstance == VIRTUAL_DEVICE_INSTANCE) { ... }`
block at the top of each one.

**If you add a case to the wrong branch** (e.g. a `GetPropertyReal` case for
a new virtual-device object placed after the `if (deviceInstance !=
g_deviceInstance) return false;` gateway guard, instead of inside the virtual
branch above it), the result is not a compile error and not even a decline -
it is `unknown-object` or `read-access-denied` for the virtual device only,
while the identically-named gateway object keeps working. Test EVERY new
property on BOTH devices, not just the one you were adding it for, and
address the virtual device with a routed request (DNET 100 - see
[Reviewing your device](#reviewing-your-device)) so you are actually
exercising the branch you changed.

## What each object type needs you to serve

The application must serve every REQUIRED property the stack does not
generate. It differs per type - this is the checklist, so you do not have to
infer it:

| Object type | You must serve | Plus |
|---|---|---|
| Analog Input | `Present_Value` (Real), `Object_Name`, `Units` | - |
| Binary Input | `Present_Value` (Enumerated), `Object_Name` | `Polarity` |
| Multi-State Input | `Present_Value` (Unsigned), `Object_Name` | `Number_Of_States` |
| Analog Output (commandable) | `Object_Name`, `Units`, `Relinquish_Default` | Priority_Array slots via `GetCommandable()` |
| Binary Output (commandable) | `Object_Name`, `Polarity`, `Relinquish_Default` | Priority_Array slots via `GetCommandable()` |
| Multi-State Output (commandable) | `Object_Name`, `Number_Of_States`, `Relinquish_Default` | Priority_Array slots via `GetCommandable()` |

The virtual device's own Analog Input follows the same Analog Input row -
it is a second, independent object of that type, not a special case.

## Who serves what: the application or the stack?

The single most common question when reading this file is "who answers this
property?" For the gateway's commandable Analog Output 1 (Chartreuse) - the
most involved object in this example, since its `Present_Value`,
`Priority_Array` and `Current_Command_Priority` are resolved by the stack
from slots the application only stores:

| Property | Served by | How |
|---|---|---|
| `Object_Identifier` | **stack** | generated from the object you added |
| `Object_Type` | **stack** | generated |
| `Object_List` | **stack** | generated (Device object) |
| `Property_List` | **stack** | generated |
| `Status_Flags` | **stack** | generated |
| `Event_State` | **stack**, sort of | no intrinsic alarming here, so nothing serves it - it reads `normal` only because `normal` is the enumeration's zero value and the stack substitutes a datatype default. Correct by coincidence, not design. |
| `Out_Of_Service` | **you** | `GetPropertyBool` - matched on object **type only** |
| `Present_Value` | **stack**, from your data | the stack resolves the highest-priority non-null `Priority_Array` slot (or `Relinquish_Default` if all are null); your `GetPropertyReal` only answers individual `Priority_Array[i]` reads via `ReadPrioritySlot()` |
| `Priority_Array` | **stack**, from your data | same mechanism - `GetPropertyBool` reports whether slot *i* is set, `GetPropertyReal` reports its value when it is |
| `Current_Command_Priority` | **stack** | computed from which slot is highest-priority and set |
| `Relinquish_Default` | **you** | `GetPropertyReal` |
| `Object_Name` | **you** | `GetPropertyCharString` |
| `Units` | **you** | `GetPropertyEnumerated` |

A write (`SetPropertyReal` for this object) stores the value in the slot for
the request's priority via `CommandWrite()`; a NULL write
(`SetPropertyNull`) relinquishes that slot via `CommandRelinquish()`. The
stack, not the application, decides what `Present_Value` reads back
afterwards.

Every object, on both devices, is in [docs/PICS.md](docs/PICS.md).

Going beyond this (alarming, COV, scheduling, physical inter-datalink
routing) means implementing a richer or different profile - see the series
table in [README.md](README.md).

## Reviewing your device

After you have changed anything, review it against the conformance statement
rather than against "it looked fine in the explorer":

1. Regenerate [docs/PICS.md](docs/PICS.md) after editing `docs/objects.json`
   (see [Keeping the PICS honest](#keeping-the-pics-honest) below). A ⚠ row
   is a required property nothing serves.
2. Read **every** property listed for **every** object on **both** devices
   with a BACnet client, and compare the value against the PICS.
   `"undefined"`, `no-units` and `0` are the three shapes a missed callback
   takes. Reading the virtual device's objects needs a routing-aware client
   (or DNET 100 / MAC addressed manually) - see README's Verify section for
   why an unrouted request to the virtual device's `Object_Identifier`
   correctly fails with `unknown-object`.
3. Diff a new object of a type against the existing one of that type -
   including diffing the virtual device's Analog Input against the gateway's
   own Analog Input 1, which must differ (different `Present_Value`) while
   both otherwise behave identically. Anything that differs and shouldn't is
   a callback that matched on instance instead of intent, or - specific to
   this example - matched on the wrong `deviceInstance` branch.
4. Confirm the services you do **not** implement are still rejected: for the
   gateway, anything outside DS-RP-B/DS-WP-B/DM-DDB-B/DM-DOB-B/DM-DCC-B; for
   the virtual device, WriteProperty and DeviceCommunicationControl (neither
   is claimed for it).
5. WriteProperty a commandable output at a priority, re-read it, then write
   NULL at that priority and confirm it falls back to `Relinquish_Default`.

### Keeping the PICS honest

`docs/PICS.md` is partly generated. `docs/objects.json` describes each object
- one entry per object, per device - and who serves which property; the
series tool regenerates the object tables from it plus the stack's own
`docs/property-profile-reference.md` at the pinned commit:

```bash
python tools/gen-objects-properties.py BACnetProfileExample-B-GW-CPP            # rewrite
python tools/gen-objects-properties.py BACnetProfileExample-B-GW-CPP --check    # fail if stale
```

(That tool lives in the example-series repository, not in this one. If you
only have this repository, edit the generated block by hand and keep it
matching the callbacks in `main.cpp`.)

When you add an object or a property to `main.cpp`, update
`docs/objects.json` in the same change and regenerate. The `app` list is what
the callbacks serve; `stack` is a device-wide fact the stack itself computes
(protocol version/revision, supported services/object types, the live object
list and address-binding table); `accepted` is for a required property you
deliberately leave to the stack's other configured default, each one
justified in the object's `note`. Anything required, not in `app`, not in
`stack`, and not in `accepted` comes out as a ⚠ row - that is a defect, not a
feature. Remember there are **two Device entries** (389019 and 389119) and
**two Network Port entries besides the gateway's physical one** (both
stack-created, for the virtual network) - a change to one device's objects
rarely needs a change to the other's.

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| A ReadProperty naming the virtual device's `Object_Identifier` (`device,389119`) gets `unknown-object` when sent as a plain, unrouted request | **Expected, not your bug.** The virtual device is not on the gateway's local object table - address it via DNET 100 (see README's "F-GATEWAY" section), or use a routing-aware client that resolves this automatically from the virtual device's own I-Am / a Who-Is-Router-To-Network reply. |
| An unrestricted Who-Is sent directly to the gateway's socket returns I-Am from the gateway only, not the virtual device | **Expected.** A local (non-DNET) Who-Is is scoped to the local network, same as it would be for a device behind a real physical router; broadcast (or restrict) it to DNET 100 to reach the virtual device, exactly as ASHRAE 135 cl. 6.6 describes for any routed network. |
| On start-up the app prints one red `Error:` line about a UUID not being set | **Expected - this is not your bug.** The stack starts a BACnet/SC datalink this BACnet/IP-only example never configures. Same as every other example in the series. |
| CMake error: *"CAS BACnet Stack adapter not found under: ..."* | Submodules not initialized. Run `git submodule update --init --recursive` (or pass `-D CAS_STACK_DIR=...`). |
| `CASBACnetStackDLL.h: No such file or directory` | Same - submodules not checked out. |
| Windows: *"No CMAKE_CXX_COMPILER could be found"* | Install Visual Studio with the "Desktop development with C++" workload, then re-run from a fresh terminal. |
| First build seems stuck for minutes | Normal - it's compiling ~600 stack files. Only the first build is slow. |
| App prints *"Failed to bind UDP port 47808"* | Another BACnet program is already using 47808. Stop it, or run with `--port <n>`. |
| Client sends Who-Is but sees no I-Am from either device | Firewall is blocking UDP 47808, or the client and device are on different subnets (Who-Is is a broadcast). Allow the port; test on the same subnet first. |
| WriteProperty to a commandable output returns `read-access-denied` | Confirm the request targets `Present_Value` on the GATEWAY's instance (`g_deviceInstance`), not the virtual device (it has no writable objects), and that the priority is 1-16. |

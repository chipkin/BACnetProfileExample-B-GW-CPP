# B-GW Wire Tests

Tests that drive the running gateway over BACnet/IP from outside it and fail
when its Profile Function - carrying data between a foreign bus and BACnet,
and reporting a foreign device that stops answering - is removed. They put
hand-built frames on the wire and check what comes back. They are written
against the series' shared harness in [`../wiretest/`](../wiretest/README.md)
(vendored, like `common/`); everything specific to this gateway is here.
Python 3 standard library only - nothing to `pip install`.

## Running

Build the gateway (README "Build"), then from the repository root:

```bash
python3 tests/run_wire_tests.py --exe build/BACnetExampleBGW
```

It runs the four scripts below once with `--mode eo` and once with
`--mode vn` and ends with `ALL WIRE TESTS PASSED` (exit 0) or the runs that
failed. Each script also runs on its own with `--exe`, `--port` (default
47890) and `--mode eo|vn`.

**Linux/macOS.** One check in `router_test.py` captures what the gateway
*broadcasts*, with a second socket on its own UDP port. POSIX `SO_REUSEADDR`
allows that; on Windows `common/` binds with `SO_EXCLUSIVEADDRUSE` on purpose,
which refuses it, so there `router_test.py --mode vn` runs without that
control (9 checks, not 10) and the KNOWN GAP is always reported. Every other
check runs on Windows.

A `[KNOWN GAP]` line is a requirement the gateway does not meet yet because of
a defect outside this example (the stack). It is reported on every run but
does not fail it; [`../TODO.md`](../TODO.md) says where it has to be fixed. If
one prints `[GAP CLOSED]`, that defect is fixed: make it a counted check.

## The setup

All on one host (`gw_setup.py`):

```
client (127.0.0.1, ephemeral UDP port) --BACnet/IP--> gateway, UDP --port   (BACnetExampleBGW --mode eo|vn)
SimulatedBus                           --control----> 127.0.0.1, UDP --port + 1
```

The foreign bus is Simulation inside the gateway process. The tests reach it
through a test-only control socket (`simulation/SimulationControl.cpp`) that
the gateway opens only when the environment variable
`BGW_SIMULATION_CONTROL_PORT` is set - never through a command-line option,
because nothing it does changes the gateway's BACnet behaviour; it changes
what the pretend field devices do (`offline`, `online`, `set`, and `get` to
look inside a register). So the Peers here are the simulated slaves, driven
from Python; the BACnet client is Python too.

In `--mode vn` the tests learn each virtual device's address the way any
client does - from the I-Am it answers a Who-Is (DNET 65535) with: SNET 100,
SADR = its address - and then read and write it through the gateway with
DNET 100 / DADR.

## `discovery_test.py` - discovery, the way a client does it

The series' discovery Wire Test (from `BACnetProfileExample-B-SS-CPP`), on the
gateway started as the other scripts start it: a unicast Who-Is gets the I-Am
of Device 389019 (sent once more, with a visible `[RETRY]` line, if no I-Am
comes back in 3 s - Who-Is is unconfirmed and the gateway's socket can drop a
datagram on Windows); Device `Object_List` names exactly the objects `main.cpp`
creates (the control) - `--mode eo` 11 (the Device, Network Port 1 and each
slave's Analog Input, Analog Output and Binary Output), `--mode vn` 3 (the
Device and Network Ports 1 and 2); then every property in every object's
`Property_List`, plus the four cl. 12.1.1.4.1 leaves out, reads as a value.
Network Port 1 `IP_DNS_Server`, `FD_BBMD_Address` and `FD_Subscription_Lifetime`
are checked by name (B-SS-CPP #20-#22). Only the gateway's own Device is walked:
in `--mode vn` the slaves' virtual devices 389119-389121 are separate devices,
read by `mapping_test.py`.

## `mapping_test.py` - the Profile Function (GW-EO-B / GW-VN-B, DS-RP-B, DS-WP-B)

On slave 2 (Air Handler):

- **BACnet Surface:** the gateway's Object_List holds exactly 11 objects in eo
  (Device, Network Port, 3 x Analog Input / Analog Output / Binary Output) and
  3 in vn (Device and its two Network Ports); each virtual device holds 5
  (Device, the three mapped objects, its stack-created Network Port).
- **Input:** the input register is set to 234 on the bus; Analog Input's
  Present_Value follows to 23.4 (and was not 23.4 before - the control).
- **Commandable output** (Annex H.2.1.3, cl. 19.2): Analog Output = 21.5 @
  priority 8 is acknowledged and the holding register becomes 215;
  Priority_Array[8] = 21.5 and Priority_Array[0] (its size) = 16; 18.0 @ 12 is
  acknowledged and fills slot 12 but does not reach the register (8 wins);
  relinquishing 8 makes 18.0 win (register 180); relinquishing 12 empties all
  16 slots (each read answers, NULL) and the register falls back to
  Relinquish_Default (130). 5000.0 - beyond the 16-bit register - is refused
  with VALUE_OUT_OF_RANGE and the register keeps 130.
- **Binary Output:** writing the opposite of Relinquish_Default @ 8 sets the
  coil; relinquishing restores it.
- vn only: every virtual device answers Who-Is from network 100, and each one
  shows its own slave (389119 the Boiler, 389120 the Air Handler).

## `failure_mode_test.py` - the Failure Mode

Slave 3 (Chiller) is pulled offline, slave 1 stays up.

- **Control:** Analog Input, Analog Output and Binary Output of slave 3 read
  Reliability `no-fault-detected`, FAULT clear.
- **Offline:** all three read Reliability `communication-failure` and the
  FAULT flag (Annex H.2.3.2); Present_Value still reads a value; slave 1's
  Analog Input stays healthy; a WriteProperty is still acknowledged (H.2.1.1)
  and the register does not change while the slave is away.
- **vn only:** no I-Am for virtual device 389121 while the gateway and the
  other two still answer (Annex H.2.2.1). KNOWN GAP: a ReadProperty to it is
  still answered - H.2.2.1 wants silence (TODO.md).
- **Revived:** no-fault again, the write made while offline lands (register
  95), the input tracks the register again (5.5); vn: 389121 answers Who-Is.

## `router_test.py` - NM-RC-B (Annex K.7.1)

- **vn:** Who-Is-Router-To-Network (no DNET) is answered with
  I-Am-Router-To-Network listing 100; with DNET 100 the same; with DNET 99 no
  I-Am-Router-To-Network(99); the `r` key broadcasts I-Am-Router-To-Network(100)
  and the broadcast listener sees it (the control for the next line); KNOWN
  GAP: the answer to Who-Is-Router-To-Network is unicast, not broadcast
  (cl. 6.4.2). A ReadProperty to DNET 100 / DADR 389119 is answered from SNET
  100 with that SADR, naming the Boiler. The stack-created Network Ports are
  named: the gateway's Network Port 2 "Vermilion 2", the virtual device's
  Network Port 1 "Vermilion".
- **eo (the control):** the same query gets no I-Am-Router-To-Network at all,
  while a ReadProperty to the same gateway in the same run is answered.

## Each check fails without its feature

Run 2026-10-14 against stack `6.x` @ `e6de4ffd`, STATIC build, WSL Ubuntu
22.04. Each row is one change to `main.cpp` in a scratch copy, rebuilt, and
the named runs repeated (all passed before the change). Checks were added
while this was being done, so each total is out of the test set of its day:
21 (mapping eo) / 23 (mapping vn) for the rows down to "The virtual network";
22 / 25 and router vn 10 for the BACnet Surface and Network Port name rows;
24 (mapping eo) for the range-check row. router vn stops early once no virtual
device answers. The final set is 24 / 27 (mapping), 15 / 19 (failure_mode),
2 / 10 (router), eo / vn.

| Without | Run | Result |
|---|---|---|
| The poll result stored in the cache (`measuredTemperature` never set) | mapping eo | 20/21 - Analog Input stays 0.0 instead of following to 23.4 |
| | mapping vn | 21/23 - the same, and the per-virtual-device check |
| | failure_mode eo | 14/15 - "tracks the register again" |
| Write-through (`ServiceWriteThrough` returns at once) | mapping eo / vn | 17/21, 19/23 - holding register stays 130, coil stays 1 |
| | failure_mode eo | 14/15 - the write made while offline never lands |
| Relinquish (`SetPropertyNull` leaves the slot set) | mapping eo / vn | 16/21, 18/23 - register stays 215, slots 8 and 12 stay set, Present_Value 21.5, coil not restored |
| The 16 priorities (every write lands in slot 16) | mapping eo / vn | 14/21, 16/23 - Priority_Array[8]/[12] NULL, 18.0 overrides 21.5 |
| Reliability (always `no-fault-detected`) | failure_mode eo / vn | 12/15, 16/19 - all three objects stay healthy, FAULT clear, while the slave is offline |
| Recovery (`SlaveCameBack` never runs) | failure_mode eo / vn | 11/15, 14/19 - objects stay in fault, the cached write never lands; vn: 389121 never answers Who-Is again |
| I-Am suppression (Who-Is not switched off for an offline virtual device) | failure_mode vn | 18/19 - I-Am from 389121 while its slave is offline |
| The virtual network (`--mode vn` builds the eo objects instead) | router vn | 1/6 - no I-Am-Router-To-Network, no virtual devices |
| | mapping vn | 0/1 - no virtual device answers from network 100 |
| A B-SS template object kept (a Binary Input added beside each slave's objects) | mapping eo / vn | 21/22, 24/25 - Object_List 14 instead of 11, 6 instead of 5 |
| The Network Port names (`Vermilion 2` case removed) | router vn | 9/10 - Network Port 2 reads `"undefined"` |
| The Analog Output range check (any REAL accepted) | mapping eo | 22/24 - 5000.0 gets a SimpleACK and the register wraps to 50000 |

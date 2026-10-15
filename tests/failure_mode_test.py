#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# Public-domain example code (CC0) - see ../LICENSE.
"""The Failure Mode: a foreign device stops answering, then comes back.

Slave 3 (Chiller) has its cable pulled on the simulated bus; slaves 1 and 2 stay up.

--mode eo (GW-EO-B, Annex H.2.3.2: "Communication errors between the gateway and the
non-BACnet device should be indicated via the Reliability property and Fault flag in
the associated BACnet objects" ... "Properties that exist in objects in the gateway
device shall return values when read and not errors, even when the non-BACnet device
... is offline"):

  - before: Reliability = no-fault-detected and Status_Flags FAULT clear on Analog
    Input 3, Analog Output 3 and Binary Output 3 (the control);
  - after the slave goes offline: all three report Reliability = communication-failure
    and the FAULT flag; Present_Value still reads (a value, not an error); the healthy
    slave 1's Analog Input 1 stays no-fault (only the dead slave's objects fault);
  - a WriteProperty to Analog Output 3 while offline is still acknowledged (Annex
    H.2.1.1 write caching), and lands in the register once the slave is back;
  - after the slave is revived: Reliability back to no-fault, FAULT clear, and the input
    tracks the register again.

--mode vn (GW-VN-B, Annex H.2.2.1):

  - before: the Chiller's virtual device 389121 answers Who-Is (the control);
  - after the slave goes offline: no I-Am for 389121 ("it shall also not send I-Am ...
    on behalf of that device"), while 389119 and 389120 still answer;
  - "the gateway shall remain silent" to requests directed to it: the stack has no
    customer export to silence a virtual device, so this is a KNOWN GAP (TODO.md).
    What the example does instead, checked here: the read is answered from the cache
    with Reliability = communication-failure and the FAULT flag, never stale-but-healthy;
  - after revive: 389121 answers Who-Is again and its objects are no-fault.

    python3 tests/failure_mode_test.py --mode eo|vn [--exe build/BACnetExampleBGW] [--port 47890]

Exit code 0 = every check passed ([KNOWN GAP] lines do not fail it).
"""
import sys

import gw_setup as setup
from gw_setup import wiretest

UNIT = 3      # the Chiller - pulled offline
HEALTHY = 1   # the Boiler - stays up

FAULT = wiretest.STATUS_FLAG_FAULT
OBJECT_TYPES = (wiretest.OBJECT_TYPE_ANALOG_INPUT, wiretest.OBJECT_TYPE_ANALOG_OUTPUT, wiretest.OBJECT_TYPE_BINARY_OUTPUT)


def health(points, object_type):
    """(Reliability, FAULT flag) of one object, or (None, None) if either read fails."""
    reliability = points.read(object_type, wiretest.PROPERTY_RELIABILITY).value
    flags = setup.status_flags(points.read(object_type, wiretest.PROPERTY_STATUS_FLAGS))
    return reliability, (flags[FAULT] if flags else None)


def all_report(points, reliability, fault):
    return all(health(points, object_type) == (reliability, fault) for object_type in OBJECT_TYPES)


def check_health(points, reliability, fault, when):
    reliability_name = "communication-failure" if reliability == wiretest.RELIABILITY_COMMUNICATION_FAILURE else "no-fault-detected"
    for object_type in OBJECT_TYPES:
        observed = health(points, object_type)
        wiretest.record(f"{when}: {points.label(object_type)} Reliability = {reliability_name}, FAULT "
                        f"{'set' if fault else 'clear'}", observed == (reliability, fault),
                        f"Reliability={observed[0]!r} FAULT={observed[1]!r}")


def main():
    args = setup.parser(__doc__).parse_args()
    gateway = setup.start_gateway(args)
    client = wiretest.Client()
    bus = setup.SimulatedBus(args)
    no_fault = wiretest.RELIABILITY_NO_FAULT_DETECTED
    communication_failure = wiretest.RELIABILITY_COMMUNICATION_FAILURE
    analog_input = wiretest.OBJECT_TYPE_ANALOG_INPUT
    analog_output = wiretest.OBJECT_TYPE_ANALOG_OUTPUT
    present_value = wiretest.PROPERTY_PRESENT_VALUE
    chiller_device = setup.VIRTUAL_DEVICE_INSTANCES[UNIT]
    try:
        virtual_addresses = setup.learn_virtual_addresses(client, args) if args.mode == "vn" else None
        if args.mode == "vn":
            wiretest.record(f"control: virtual device {chiller_device} answers Who-Is while its slave is online",
                            chiller_device in virtual_addresses, f"I-Am from {sorted(virtual_addresses)}")
            if chiller_device not in virtual_addresses:
                return setup.finish(gateway)
        points = setup.Points(args, client, UNIT, virtual_addresses)
        healthy = setup.Points(args, client, HEALTHY, virtual_addresses)

        # Before: healthy (the control - a fault that is always set proves nothing).
        setup.wait_until(lambda: all_report(points, no_fault, False))
        check_health(points, no_fault, False, "slave online")

        # Pull the cable.
        bus.offline(UNIT)
        setup.wait_until(lambda: all_report(points, communication_failure, True))
        check_health(points, communication_failure, True, "slave offline")
        reading = points.read(analog_input, present_value)
        why = "Annex H.2.3.2" if args.mode == "eo" else "the documented vn behaviour while H.2.2.1 silence is blocked"
        wiretest.record(f"slave offline: Analog Input Present_Value still answers with a value, not an error ({why})",
                        reading.ok and isinstance(reading.value, float), reading.describe())
        healthy_state = health(healthy, analog_input)
        wiretest.record(f"slave offline: the healthy {healthy.label(analog_input)} stays no-fault",
                        healthy_state == (no_fault, False), f"Reliability={healthy_state[0]!r} FAULT={healthy_state[1]!r}")

        written = points.write(analog_output, present_value, wiretest.application_real(9.5), priority=8)
        wiretest.record("slave offline: WriteProperty Analog Output = 9.5 @ 8 is still acknowledged (Annex H.2.1.1)",
                        written.ok, written.describe())
        setup.wait_until(lambda: False, seconds=1.0)
        wiretest.record("precondition: ... the register does not change while the slave is offline",
                        bus.get(UNIT, setup.HOLDING_REGISTER) != 95, f"register = {bus.get(UNIT, setup.HOLDING_REGISTER)}")

        if args.mode == "vn":
            answers = setup.who_is_everywhere(client, args)
            others = [setup.VIRTUAL_DEVICE_INSTANCES[unit] for unit in (1, 2)]
            wiretest.record(f"slave offline: no I-Am on behalf of virtual device {chiller_device} (Annex H.2.2.1)",
                            chiller_device not in answers, f"I-Am from {sorted(answers)}")
            wiretest.record("control: ... the online slaves' virtual devices and the gateway still answer Who-Is",
                            all(instance in answers for instance in others + [setup.GATEWAY_DEVICE_INSTANCE]),
                            f"I-Am from {sorted(answers)}")
            offline_read = points.read(analog_input, present_value)
            wiretest.known_gap(f"slave offline: virtual device {chiller_device} stays silent to a ReadProperty "
                               f"(Annex H.2.2.1) - got {offline_read.describe()}", not offline_read.answered,
                               "the stack has no customer export to silence a virtual device (TODO.md)")

        # Revive.
        bus.online(UNIT)
        setup.wait_until(lambda: all_report(points, no_fault, False))
        check_health(points, no_fault, False, "slave back")
        wiretest.record("slave back: the write made while it was offline lands - holding register = 95",
                        setup.wait_until(lambda: bus.get(UNIT, setup.HOLDING_REGISTER) == 95),
                        f"register = {bus.get(UNIT, setup.HOLDING_REGISTER)}")
        bus.set(UNIT, setup.INPUT_REGISTER, 55)
        tracked = setup.wait_until(lambda: (lambda value: value is not None and abs(value - 5.5) < 0.05)(
            points.read(analog_input, present_value).value))
        wiretest.record("slave back: Analog Input tracks the register again (5.5)", tracked,
                        f"reads {points.read(analog_input, present_value).value!r}")
        if args.mode == "vn":
            answers = setup.wait_until(lambda: (lambda found: found if chiller_device in found else None)(
                setup.who_is_everywhere(client, args, 1.0)))
            wiretest.record(f"slave back: virtual device {chiller_device} answers Who-Is again", bool(answers),
                            f"I-Am from {sorted(answers or {})}")
    finally:
        bus.close()
        client.close()
        gateway.stop()
    return setup.finish(gateway)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# Public-domain example code (CC0) - see ../LICENSE.
"""The Profile Function: foreign data in, BACnet commands out (GW-EO-B / GW-VN-B, DS-RP-B, DS-WP-B).

Against the running gateway, in the --mode given (eo or vn), on slave 2 (Air Handler):

  1. Input: the slave's input register is changed on the simulated bus (as its sensor
     would); Analog Input <n>'s Present_Value follows within a poll cycle - and the
     value it showed before was not already the new one (the control).
  2. Commandable output (Annex H.2.1.3, all 16 Priority_Array entries per cl. 19.2):
     WriteProperty Analog Output Present_Value = 21.5 @ priority 8 is acknowledged, the
     slave's holding register becomes 215, Priority_Array[8] reads 21.5. A second write
     of 18.0 @ priority 12 does NOT reach the register (priority 8 still wins) but does
     fill Priority_Array[12]. Relinquishing priority 8 (NULL) makes 18.0 win; relinquishing
     12 too leaves every slot NULL and the register falls back to Relinquish_Default.
  3. Binary Output: active @ priority 8 sets the slave's coil; relinquishing restores
     Relinquish_Default.
  4. In --mode vn the same objects are read and written THROUGH the gateway (DNET 100, DADR
     = the virtual device), and each virtual device answers for its own slave only.

    python3 tests/mapping_test.py --mode eo|vn [--exe build/BACnetExampleBGW] [--port 47890]

Exit code 0 = every check passed.
"""
import sys

import gw_setup as setup
from gw_setup import wiretest

UNIT = 2   # the Air Handler


def main():
    args = setup.parser(__doc__).parse_args()
    gateway = setup.start_gateway(args)
    client = wiretest.Client()
    bus = setup.SimulatedBus(args)
    try:
        virtual_addresses = None
        if args.mode == "vn":
            virtual_addresses = setup.learn_virtual_addresses(client, args)
            wiretest.record("vn: every virtual device answers Who-Is from network 100",
                            set(virtual_addresses) == set(setup.VIRTUAL_DEVICE_INSTANCES.values()),
                            f"I-Am from {sorted(virtual_addresses)}")
            if set(virtual_addresses) != set(setup.VIRTUAL_DEVICE_INSTANCES.values()):
                return setup.finish(gateway)
        points = setup.Points(args, client, UNIT, virtual_addresses)

        # The BACnet Surface is exactly the profile's set: Object_List[0] is the number of objects.
        # eo: Device + Network Port + 3 slaves x (AI, AO, BO) = 11. vn: the gateway is Device +
        # its two Network Ports (BACnet/IP, virtual network 100) = 3, and each virtual device is
        # Device + AI + AO + BO + its stack-created Network Port = 5.
        gateway_objects = client.read_property(args.port, wiretest.OBJECT_TYPE_DEVICE, setup.GATEWAY_DEVICE_INSTANCE,
                                               wiretest.PROPERTY_OBJECT_LIST, array_index=0).value
        expected = 11 if args.mode == "eo" else 3
        wiretest.record(f"BACnet Surface: the gateway's Object_List has {expected} objects", gateway_objects == expected,
                        f"Object_List[0] = {gateway_objects!r}")
        if args.mode == "vn":
            virtual_objects = client.read_property(args.port, wiretest.OBJECT_TYPE_DEVICE, setup.VIRTUAL_DEVICE_INSTANCES[UNIT],
                                                   wiretest.PROPERTY_OBJECT_LIST, array_index=0, **points.routing).value
            wiretest.record("BACnet Surface: a virtual device's Object_List has 5 objects", virtual_objects == 5,
                            f"Object_List[0] = {virtual_objects!r}")
        analog_input = wiretest.OBJECT_TYPE_ANALOG_INPUT
        analog_output = wiretest.OBJECT_TYPE_ANALOG_OUTPUT
        binary_output = wiretest.OBJECT_TYPE_BINARY_OUTPUT
        present_value = wiretest.PROPERTY_PRESENT_VALUE
        priority_array = wiretest.PROPERTY_PRIORITY_ARRAY

        # 1. The input follows the foreign register.
        name = points.read(analog_input, wiretest.PROPERTY_OBJECT_NAME)
        wiretest.record(f"{points.label(analog_input)} exists", name.ok, name.describe())
        before = points.read(analog_input, present_value).value
        bus.set(UNIT, setup.INPUT_REGISTER, 234)   # 23.4 C
        wiretest.record("control: Analog Input was not already 23.4 before the register changed",
                        before is not None and abs(before - 23.4) > 0.05, f"was {before!r}")
        after = setup.wait_until(lambda: (lambda value: value if value is not None and abs(value - 23.4) < 0.05 else None)(
            points.read(analog_input, present_value).value))
        wiretest.record(f"{points.label(analog_input)} Present_Value follows the input register to 23.4",
                        after is not None, f"reads {points.read(analog_input, present_value).value!r}")

        # 2. The commandable Analog Output writes through to the holding register.
        default = setup.SETPOINT_RELINQUISH_DEFAULTS[UNIT]
        relinquish_default = points.read(analog_output, wiretest.PROPERTY_RELINQUISH_DEFAULT).value
        wiretest.record(f"{points.label(analog_output)} Relinquish_Default = {default}",
                        relinquish_default is not None and abs(relinquish_default - default) < 0.01,
                        f"reads {relinquish_default!r}")
        register = lambda: bus.get(UNIT, setup.HOLDING_REGISTER)
        wiretest.record("precondition: the holding register starts at Relinquish_Default x 10",
                        setup.wait_until(lambda: register() == round(default * 10)), f"register = {register()}")

        written = points.write(analog_output, present_value, wiretest.application_real(21.5), priority=8)
        wiretest.record("WriteProperty Analog Output = 21.5 @ priority 8 is acknowledged", written.ok, written.describe())
        wiretest.record("... and reaches the slave: holding register = 215",
                        setup.wait_until(lambda: register() == 215), f"register = {register()}")
        slot_8 = points.read(analog_output, priority_array, array_index=8).value
        wiretest.record("... Priority_Array[8] = 21.5", slot_8 is not None and abs(slot_8 - 21.5) < 0.01, f"{slot_8!r}")
        size = points.read(analog_output, priority_array, array_index=0).value
        wiretest.record("Priority_Array has 16 entries (cl. 19.2, Annex H.2.1.3)", size == 16, f"Priority_Array[0] = {size!r}")

        written = points.write(analog_output, present_value, wiretest.application_real(18.0), priority=12)
        wiretest.record("WriteProperty Analog Output = 18.0 @ priority 12 is acknowledged", written.ok, written.describe())
        slot_12 = points.read(analog_output, priority_array, array_index=12).value
        wiretest.record("... Priority_Array[12] = 18.0", slot_12 is not None and abs(slot_12 - 18.0) < 0.01, f"{slot_12!r}")
        setup.wait_until(lambda: False, seconds=1.0)
        wiretest.record("... but priority 8 still wins: register stays 215, Present_Value 21.5",
                        register() == 215 and abs((points.read(analog_output, present_value).value or 0) - 21.5) < 0.01,
                        f"register = {register()}")

        relinquished = points.write(analog_output, present_value, wiretest.application_null(), priority=8)
        wiretest.record("WriteProperty NULL @ priority 8 (relinquish) is acknowledged", relinquished.ok, relinquished.describe())
        wiretest.record("... priority 12 now wins: holding register = 180",
                        setup.wait_until(lambda: register() == 180), f"register = {register()}")
        relinquished = points.write(analog_output, present_value, wiretest.application_null(), priority=12)
        wiretest.record("WriteProperty NULL @ priority 12 is acknowledged", relinquished.ok, relinquished.describe())
        wiretest.record(f"... every slot NULL: holding register falls back to Relinquish_Default ({round(default * 10)})",
                        setup.wait_until(lambda: register() == round(default * 10)), f"register = {register()}")
        slots = [points.read(analog_output, priority_array, array_index=slot) for slot in range(1, 17)]
        wiretest.record("... and all 16 Priority_Array entries read NULL",
                        all(slot.ok and slot.value is None for slot in slots),
                        f"{[slot.describe() for slot in slots]}")
        current = points.read(analog_output, present_value).value
        wiretest.record(f"... Present_Value = Relinquish_Default {default}",
                        current is not None and abs(current - default) < 0.01, f"{current!r}")

        # A value the 16-bit register cannot hold is refused, not acknowledged and wrapped.
        refused = points.write(analog_output, present_value, wiretest.application_real(5000.0), priority=8)
        wiretest.record("WriteProperty Analog Output = 5000.0 (beyond the register) is refused: Error VALUE_OUT_OF_RANGE",
                        refused.error is not None and refused.error[1] == 37, refused.describe())
        setup.wait_until(lambda: False, seconds=1.0)
        wiretest.record("... and the register keeps Relinquish_Default", register() == round(default * 10),
                        f"register = {register()}")

        # 3. The Binary Output writes through to the coil.
        coil = lambda: bus.get(UNIT, setup.COIL)
        enable_default = setup.ENABLE_RELINQUISH_DEFAULTS[UNIT]
        target = 0 if enable_default else 1
        written = points.write(binary_output, present_value, wiretest.application_enumerated(target), priority=8)
        wiretest.record(f"WriteProperty Binary Output = {'active' if target else 'inactive'} @ priority 8 is acknowledged",
                        written.ok, written.describe())
        wiretest.record(f"... and reaches the slave: coil = {target}", setup.wait_until(lambda: coil() == target),
                        f"coil = {coil()}")
        points.write(binary_output, present_value, wiretest.application_null(), priority=8)
        wiretest.record(f"... relinquished: coil back to Relinquish_Default ({enable_default})",
                        setup.wait_until(lambda: coil() == enable_default), f"coil = {coil()}")

        # 4. vn: each virtual device is its own slave.
        if args.mode == "vn":
            other = setup.Points(args, client, 1, virtual_addresses)
            bus.set(1, setup.INPUT_REGISTER, 777)
            value = setup.wait_until(lambda: (lambda reading: reading if reading is not None and abs(reading - 77.7) < 0.05 else None)(
                other.read(analog_input, present_value).value))
            mine = points.read(analog_input, present_value).value
            wiretest.record("vn: virtual device 389119 shows the Boiler's register, 389120 still the Air Handler's",
                            value is not None and mine is not None and abs(mine - 23.4) < 0.05,
                            f"389119 AI = {value!r}, 389120 AI = {mine!r}")
    finally:
        bus.close()
        client.close()
        gateway.stop()
    return setup.finish(gateway)


if __name__ == "__main__":
    sys.exit(main())

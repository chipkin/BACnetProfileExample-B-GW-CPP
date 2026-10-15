#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
# Public-domain example code (CC0) - see ../LICENSE.
"""NM-RC-B: the gateway as a router to its virtual network (Annex K.7.1, H.1.1.1).

K.7.1: a GW-VN-B device "appears to the BACnet network as a router as described in
Annex H" and "shall, at a minimum, support NM-RC-B and DS-RP-B".

--mode vn:
  - Who-Is-Router-To-Network with no DNET (cl. 6.4.1), sent to the gateway, is answered
    with I-Am-Router-To-Network listing network 100;
  - the same query naming DNET 100 is answered the same way; naming a network the
    gateway does not reach (99) gets no I-Am-Router-To-Network for 99;
  - a ReadProperty addressed through the gateway to DNET 100 / DADR = a virtual device
    reaches it: the ComplexACK comes back with SNET 100 and that device's SADR (cl. 6.5.4);
  - the 'r' key broadcasts I-Am-Router-To-Network(100) (the control for the next check);
  - KNOWN GAP: cl. 6.4.2 says I-Am-Router-To-Network is "always be transmitted with a
    broadcast MAC address"; the stack answers unicast, to the requester (TODO.md - the
    same stack defect B-RTR reports).

--mode eo (the control): the gateway is NOT a router - the same Who-Is-Router-To-Network
gets no I-Am-Router-To-Network at all.

Linux/macOS: the broadcast check listens on the gateway's own UDP port with a second
socket (wiretest.broadcast_listener), which POSIX SO_REUSEADDR allows and Windows refuses. So the vn answer comes from the virtual network
and nothing else.

    python3 tests/router_test.py --mode eo|vn [--exe build/BACnetExampleBGW] [--port 47890]

Exit code 0 = every check passed.
"""
import struct
import sys

import gw_setup as setup
from gw_setup import wiretest


def ask(client, args, dnet=None, broadcasts=None):
    """Who-Is-Router-To-Network (optionally for one DNET) sent to the gateway. Returns
    (answer, was_broadcast): the I-Am-Router-To-Network that answers (or None), and whether
    it was broadcast on the gateway's network rather than sent back to the client alone."""
    client.frames.clear()
    if broadcasts is not None:
        broadcasts.frames.clear()
    data = struct.pack(">H", dnet) if dnet is not None else b""
    client.send(wiretest.bvlc(wiretest.npdu(data, network_message=wiretest.NL_WHO_IS_ROUTER_TO_NETWORK)), args.port)
    is_answer = lambda frame: frame.network_message == wiretest.NL_I_AM_ROUTER_TO_NETWORK
    unicast = client.wait_for(is_answer, 1.5)
    broadcast = broadcasts.wait_for(is_answer, 0.5) if broadcasts is not None else None
    return (broadcast or unicast), broadcast is not None


def main():
    args = setup.parser(__doc__).parse_args()
    gateway = setup.start_gateway(args)
    client = wiretest.Client()
    # What the gateway broadcasts on its own network, read on a second socket on its UDP port
    # (POSIX SO_REUSEADDR - see wiretest.broadcast_listener). Its start-up I-Am is the TX line
    # that names the broadcast address.
    broadcast_ip = wiretest.broadcast_address(gateway.output(), args.port)
    broadcasts = wiretest.broadcast_listener(broadcast_ip, args.port) if broadcast_ip else None
    try:
        answer, was_broadcast = ask(client, args, broadcasts=broadcasts)
        if args.mode == "eo":
            wiretest.record("eo (control): Who-Is-Router-To-Network gets no I-Am-Router-To-Network - not a router",
                            answer is None, answer.describe() if answer else "no answer")
            alive = client.read_property(args.port, wiretest.OBJECT_TYPE_DEVICE, setup.GATEWAY_DEVICE_INSTANCE,
                                         wiretest.PROPERTY_OBJECT_NAME)
            wiretest.record("eo: ... while the same gateway answers a ReadProperty (it is listening, just not a router)",
                            alive.ok, alive.describe())
            return setup.finish(gateway)

        wiretest.record("Who-Is-Router-To-Network is answered with I-Am-Router-To-Network", answer is not None,
                        answer.describe() if answer else "no answer")
        networks = answer.networks() if answer else []
        wiretest.record(f"... listing virtual network {setup.VIRTUAL_NETWORK}", setup.VIRTUAL_NETWORK in networks,
                        f"lists {networks}")
        wiretest.known_gap("... sent with a broadcast MAC address (cl. 6.4.2)",
                           answer is not None and was_broadcast,
                           "the stack sends it unicast, to the requester's address (TODO.md)")

        # Control for the check above: the listener does see the gateway's broadcasts. The 'r'
        # key re-sends the start-up I-Am-Router-To-Network(100) broadcast (cl. 6.6.2).
        if broadcasts is not None:
            broadcasts.frames.clear()
            gateway.process.stdin.write(b"r")
            gateway.process.stdin.flush()
            announced = broadcasts.wait_for(lambda frame: frame.network_message == wiretest.NL_I_AM_ROUTER_TO_NETWORK, 2)
            wiretest.record("control: the 'r' key broadcasts I-Am-Router-To-Network(100), and the listener sees it",
                            announced is not None and setup.VIRTUAL_NETWORK in announced.networks(),
                            announced.describe() if announced else "no broadcast seen")

        answer, _ = ask(client, args, setup.VIRTUAL_NETWORK)
        networks = answer.networks() if answer else []
        wiretest.record(f"Who-Is-Router-To-Network({setup.VIRTUAL_NETWORK}) is answered, listing {setup.VIRTUAL_NETWORK}",
                        setup.VIRTUAL_NETWORK in networks, answer.describe() if answer else "no answer")
        answer, _ = ask(client, args, 99)
        wiretest.record("Who-Is-Router-To-Network(99) - a network it does not reach - gets no I-Am-Router-To-Network(99)",
                        answer is None or 99 not in answer.networks(), answer.describe() if answer else "no answer")

        addresses = setup.learn_virtual_addresses(client, args)
        boiler = setup.VIRTUAL_DEVICE_INSTANCES[1]
        if boiler not in addresses:
            wiretest.record("virtual device 389119 answers Who-Is from network 100", False, f"I-Am from {sorted(addresses)}")
            return setup.finish(gateway)
        read = client.read_property(args.port, wiretest.OBJECT_TYPE_DEVICE, boiler, wiretest.PROPERTY_OBJECT_NAME,
                                    dnet=setup.VIRTUAL_NETWORK, dadr=addresses[boiler])
        frame = read.frame
        wiretest.record("ReadProperty routed to DNET 100 / DADR = virtual device 389119 is answered",
                        read.ok, read.describe())
        wiretest.record("... from SNET 100 with that device's SADR (cl. 6.5.4)",
                        frame is not None and frame.snet == setup.VIRTUAL_NETWORK and frame.sadr == addresses[boiler],
                        frame.describe() if frame else "no answer")
        wiretest.record("... naming the Boiler", read.value == "Chipkin Example B-GW (Boiler)", repr(read.value))

        # The Network Ports the stack creates for the virtual network are named, not "undefined".
        port_name = client.read_property(args.port, wiretest.OBJECT_TYPE_NETWORK_PORT, 2, wiretest.PROPERTY_OBJECT_NAME).value
        wiretest.record("the gateway's Network Port 2 (virtual network 100) is named \"Vermilion 2\"",
                        port_name == "Vermilion 2", repr(port_name))
        port_name = client.read_property(args.port, wiretest.OBJECT_TYPE_NETWORK_PORT, 1, wiretest.PROPERTY_OBJECT_NAME,
                                         dnet=setup.VIRTUAL_NETWORK, dadr=addresses[boiler]).value
        wiretest.record("virtual device 389119's own Network Port 1 is named \"Vermilion\"", port_name == "Vermilion",
                        repr(port_name))
    finally:
        client.close()
        if broadcasts is not None:
            broadcasts.close()
        gateway.stop()
    return setup.finish(gateway)


if __name__ == "__main__":
    sys.exit(main())

# SPDX-License-Identifier: CC0-1.0
# Public-domain example code (CC0) - see ../LICENSE.
"""What B-GW's wire tests share: this gateway's numbers, starting it, and the simulated bus.

The setup every test uses, all on one host:

    client (127.0.0.1, ephemeral UDP port)  --BACnet/IP-->  gateway   UDP --port
                                                             (BACnetExampleBGW --mode eo|vn)
    SimulatedBus (127.0.0.1)                --test control-> 127.0.0.1 UDP --port + 1
                                                             (BGW_SIMULATION_CONTROL_PORT)

The foreign bus is SIMULATION inside the gateway process (simulation/). The tests reach
it through the gateway's test-only control socket, which is switched on by an environment
variable and never by a command-line option: it changes nothing the gateway does on the
BACnet network, only what the pretend field devices do.
"""
import argparse
import socket
import sys
import time
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY))

import wiretest  # noqa: E402  (the vendored harness at the repository root)

# Match main.cpp.
GATEWAY_DEVICE_INSTANCE = 389019
VIRTUAL_NETWORK = 100
VIRTUAL_DEVICE_INSTANCES = {1: 389119, 2: 389120, 3: 389121}   # slave unit id -> virtual device
SLAVE_NAMES = {1: "Boiler", 2: "Air Handler", 3: "Chiller"}
SETPOINT_RELINQUISH_DEFAULTS = {1: 65.0, 2: 13.0, 3: 7.0}     # Analog Output n, degrees C
ENABLE_RELINQUISH_DEFAULTS = {1: 1, 2: 1, 3: 0}               # Binary Output n

# Match simulation/SimulatedForeignBus.h: every slave has one point of each kind, at address 0,
# temperatures in degrees C x 10.
INPUT_REGISTER = "input"
HOLDING_REGISTER = "holding"
COIL = "coil"

# How long the gateway can take to notice a change on the bus: the slowest poll cycle
# (900 ms) plus the slowest slave's latency, or two unanswered polls (offline), with margin.
SETTLE_SECONDS = 4.0


def parser(description):
    arguments = argparse.ArgumentParser(description=description, formatter_class=argparse.RawDescriptionHelpFormatter)
    arguments.add_argument("--exe", default=str(wiretest.default_exe(REPOSITORY, "BACnetExampleBGW")),
                           help="path to BACnetExampleBGW (default: build/BACnetExampleBGW)")
    arguments.add_argument("--port", type=int, default=47890,
                           help="the gateway's BACnet/IP UDP port; the simulation control socket is --port + 1")
    arguments.add_argument("--mode", choices=("eo", "vn"), default="eo",
                           help="the gateway's --mode: eo (GW-EO-B, the default) or vn (GW-VN-B)")
    return arguments


def start_gateway(args, label="gateway"):
    return wiretest.start(args.exe, ["--port", args.port, "--mode", args.mode], f"{label}-{args.mode}",
                          {"BGW_SIMULATION_CONTROL_PORT": args.port + 1})


class SimulatedBus:
    """The test's hands on the simulated foreign bus, through the gateway's control socket."""

    def __init__(self, args):
        self.address = ("127.0.0.1", args.port + 1)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(2.0)

    def command(self, text):
        self.sock.sendto(text.encode("ascii"), self.address)
        reply = self.sock.recvfrom(256)[0].decode("ascii")
        if reply.startswith("error"):
            raise RuntimeError(f"simulation control '{text}': {reply}")
        return reply

    def offline(self, unit):
        """Pull the slave's cable: it answers nothing from now on."""
        self.command(f"offline {unit}")

    def online(self, unit):
        self.command(f"online {unit}")

    def set(self, unit, table, value, address=0):
        """Change a point the way the field device itself would."""
        self.command(f"set {unit} {table} {address} {value}")

    def get(self, unit, table, address=0):
        """What is REALLY in the field device's register (not what the gateway believes)."""
        return int(self.command(f"get {unit} {table} {address}").split()[1])

    def close(self):
        self.sock.close()


class Points:
    """Where slave `unit`'s three mapped objects are on the network, in either mode.

    --mode eo: objects of the gateway device, instance = the slave's unit id.
    --mode vn: objects of the slave's virtual device, instance 1, reached through the
               gateway with DNET = 100 and DADR = the virtual device's address. That
               address is learned the way any client learns it: from the I-Am the
               virtual device answers a Who-Is with (SNET = 100, SADR = its address).
    """

    def __init__(self, args, client, unit, virtual_addresses=None):
        self.args = args
        self.client = client
        self.unit = unit
        if args.mode == "eo":
            self.instance = unit
            self.routing = {}
        else:
            self.instance = 1
            self.routing = {"dnet": VIRTUAL_NETWORK, "dadr": virtual_addresses[VIRTUAL_DEVICE_INSTANCES[unit]]}

    def read(self, object_type, property_identifier, array_index=None):
        return self.client.read_property(self.args.port, object_type, self.instance, property_identifier,
                                         array_index, **self.routing)

    def write(self, object_type, property_identifier, value_octets, priority=None):
        return self.client.write_property(self.args.port, object_type, self.instance, property_identifier,
                                          value_octets, priority, **self.routing)

    def label(self, object_type):
        names = {wiretest.OBJECT_TYPE_ANALOG_INPUT: "Analog Input", wiretest.OBJECT_TYPE_ANALOG_OUTPUT: "Analog Output",
                 wiretest.OBJECT_TYPE_BINARY_OUTPUT: "Binary Output"}
        where = (f"device {GATEWAY_DEVICE_INSTANCE}" if self.args.mode == "eo"
                 else f"virtual device {VIRTUAL_DEVICE_INSTANCES[self.unit]}")
        return f"{names[object_type]} {self.instance} ({where}, slave {self.unit} {SLAVE_NAMES[self.unit]})"


def who_is_everywhere(client, args, seconds=1.5):
    """Who-Is to every network (DNET 65535), sent to the gateway. Returns {device instance: I-Am frame}.
    The stack answers a remote-network Who-Is with I-Ams addressed back to the requester."""
    client.frames.clear()
    client.send(wiretest.bvlc(wiretest.npdu(wiretest.who_is(), dnet=wiretest.GLOBAL_BROADCAST_NETWORK)), args.port)
    answers = {}
    for frame in client.collect(seconds):
        if (frame.ok and frame.network_message is None and frame.pdu_type == wiretest.PDU_UNCONFIRMED_REQUEST
                and frame.service == wiretest.SERVICE_I_AM):
            identifier = wiretest.decode_application(frame.payload[2:])
            if isinstance(identifier, tuple):
                answers[identifier[1]] = frame
    return answers


def learn_virtual_addresses(client, args):
    """{virtual device instance: SADR} from the I-Ams that come back from network 100."""
    answers = who_is_everywhere(client, args)
    return {instance: frame.sadr for instance, frame in answers.items() if frame.snet == VIRTUAL_NETWORK}


def wait_until(check, seconds=SETTLE_SECONDS, interval=0.2):
    """Poll `check()` until it returns something truthy or `seconds` pass; returns its last result."""
    deadline = time.monotonic() + seconds
    while True:
        result = check()
        if result or time.monotonic() >= deadline:
            return result
        time.sleep(interval)


def status_flags(answer):
    """The Status_Flags bits of a ReadProperty answer, or None."""
    value = answer.value
    return value if isinstance(value, list) and len(value) >= 4 else None


def finish(gateway):
    if gateway and not all(wiretest.results.RESULTS):
        gateway.keep_log()
    return wiretest.summary()

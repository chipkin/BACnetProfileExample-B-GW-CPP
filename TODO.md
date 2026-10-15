# TODO / known gaps

Every gap below is outside this example - in the CAS BACnet Stack - and each is
reported on every Wire Test run as a `[KNOWN GAP]` line (`tests/`), so the day
one is fixed the test says so (`[GAP CLOSED]`): then turn it into a counted
check and delete its entry here.

Verified 2026-10-14 against the pinned stack, `6.x` @ `e6de4ffd` (6.0.23).

## `--mode vn`: an offline virtual device cannot be made silent (Annex H.2.2.1) - blocked

Annex H.2.2.1 (135-2024): "communication timeout errors between the gateway and
the non-BACnet device shall not result in the gateway returning an error,
abort, or reject PDU in response to BACnet requests directed to the non-BACnet
device. Instead, the gateway shall remain silent." and "When a gateway has
determined that a non-BACnet device is offline, it shall also not send I-Am or
I-Have requests on behalf of that device."

- The second sentence is done: `main.cpp` switches the virtual device's Who-Is
  and Who-Has off (`BACnetStack_SetServiceEnabled`) while its slave is
  offline, and back on when it returns. `tests/failure_mode_test.py --mode vn`
  checks it.
- The first is **not possible through the customer interface**:
  - `BACnetStack_RemoveDevice` is one-way - the stack keeps the virtual
    device's router mapping, so `BACnetStack_AddDeviceToVirtualNetwork`
    refuses the same instance "for the life of the process" (its own doc
    comment). A device that comes back online could never be re-added.
  - `BACnetStack_SetServiceEnabled(..., SERVICE_READ_PROPERTY, false)` makes
    the stack answer with a Reject (unrecognized service) - an answer.
  - No `GetProperty*` callback return value means "drop this request".
- So today the virtual device keeps answering ReadProperty from the cache, with
  Reliability `communication-failure` and the FAULT flag. The Wire Test
  reports `[KNOWN GAP] slave offline: virtual device 389121 stays silent to a
  ReadProperty (Annex H.2.2.1)`.

Needs a stack export - filed as [#3932](https://github.com/chipkin/cas-bacnet-stack/issues/3932) (related: [#2873](https://github.com/chipkin/cas-bacnet-stack/issues/2873)).

## I-Am-Router-To-Network answering Who-Is-Router-To-Network is unicast (cl. 6.4.2)

Cl. 6.4.2: I-Am-Router-To-Network "shall always be transmitted with a broadcast
MAC address". The stack's virtual router answers Who-Is-Router-To-Network with
an I-Am-Router-To-Network sent to the requester only. The content (network 100)
is right. The same stack defect B-RTR reports for physical routing. Filed as
[#3931](https://github.com/chipkin/cas-bacnet-stack/issues/3931).
`tests/router_test.py --mode vn` reports it; the start-up and `r`-key
announcements, which `main.cpp` sends itself with an explicit broadcast
address, are broadcast correctly.

## Stack pin: the `6.x` tip does not link outside a test-tool build

`origin/6.x` @ `4b27bd56` fails to link any customer build
(`RecordTypedGetDecline` is defined only under `BACNET_STACK_TESTTOOL`) - stack
issue #3850, fixed on `issues/runbook`, not yet promoted. This example stays on
`e6de4ffd` until `6.x` contains the fix.

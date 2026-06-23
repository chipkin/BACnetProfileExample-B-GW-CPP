# Plan (STUB): B-GW (Gateway) — C++ example

> **STATUS: STUB.** Seed facts below. Expand from
> [`bacnet-profile-plan-template.md`](../../bacnet-profile-plan-template.md) after the
> sample plans ([B-LD](../../BACnetProfileExample-B-LD-CPP/docs/plan.md),
> [B-BC](../../BACnetProfileExample-B-BC-CPP/docs/plan.md)) are reviewed.

**Profile:** B-GW · **Family:** Annex L.7 (Miscellaneous) · **Role:** B ·
**Archetype:** Infrastructure · **Difficulty:** 4/5 · **Phase:** B (deferred — infrastructure)

**Thesis:** a gateway — represents non-BACnet (or other-protocol) devices as
**virtual BACnet devices** on a virtual network. The CAS Gateway product is exactly
this; the example shows the stack mechanism. Closely related to B-RTR (virtual
network) — build after it.

## Required BIBBs (profiles.md L.7)
`DS-RP-B, DS-WP-B; DM-DDB-B, DM-DOB-B, DM-DCC-B, GW-EO-B or GW-VN-B`.

## Services to enable
- ReadProperty (1), WriteProperty (15), DCC (17), + virtual-device routing.

## Objects (baseline + )
- One or more **virtual BACnet devices** (each with its own Device object + a few
  points) on a virtual network behind the gateway device.

## Shared features
- **DEFINE:** F-GATEWAY (GW-VN-B virtual network / GW-EO-B). GW-VN-B requires
  NM-RC-B + DS-RP-B; GW-EO-B requires only DS-RP-B (profiles.md S66).
- **REUSE:** F-DCC (B-ASC), F-OUTPUTS (B-SA), and the virtual-network mechanism
  from B-RTR (F-ROUTER).

## Known stack gaps
- Confirm the standard DLL's **virtual-device API** (how to add a virtual device on
  a virtual network and route to it). profiles.md: ✅ S66 (spec-literal flip;
  "the IUT is not actually a router, but the BIBB services are present").

## Notes / open questions
- Build **after B-RTR** (shares the virtual-network/routing mechanism). Decide how
  many virtual devices to model — one is enough to prove the profile (keep minimal).

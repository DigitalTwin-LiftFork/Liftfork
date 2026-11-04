# Stage 1 — pallet and fork contact physics

Stage 1 keeps `AAutomatedForklift` and its lift/reach/side-shift axes kinematic. Pallets remain independent Chaos rigid bodies: the forks support them through contact and do not attach or teleport them to the truck.

## Physics values

The values below are starting simulation estimates, not manufacturer specifications:

- `APallet`: empty mass defaults to 50 kg; when `bHasCargo` is enabled, `CargoMassKg` (default 800 kg) is added to the empty pallet mass.
- `AWoodPallet`: mass defaults to 100 kg.
- Both pallet types expose linear and angular damping. These reduce residual drift and spin after contact; tune them in the placed actor's Details panel.
- The root mass override compensates for the default geometric mass of attached collision boxes, so the total Chaos mass matches the configured pallet plus cargo mass. Collision shapes remain simple boxes, while visual boards do not collide.
- Continuous collision detection (CCD) is enabled on pallet collision bodies to reduce tunneling during moving-fork contact.
- A collision backrest now sits at the fork heels. It moves with lift/reach/side-shift and physically blocks a supported pallet from sliding rearward into the truck.

These values are deliberately editable. Replace the estimates with measured pallet and payload masses when available. The 1,600 kg truck rated load is a capacity reference, not the mass of every test payload.

## Manual verification in Unreal Editor

Open `Map1`, press Play, and use the existing controls:

1. Press **P** to spawn the 110 × 110 × 15 cm test pallet. In the Output Log, find `[R16HD_PALLET_PHYSICS]`; confirm the reported and target masses are about 50 kg, `sim=1`, and `gravity=1`.
2. Drive the fork tines into the pallet openings slowly. A bad height or lateral alignment should push/block against the pallet rather than make it disappear or pass through.
3. With both tines inserted, raise the forks using **E**. The pallet should rise from physical tine contact while remaining a separate actor. Lower with **Q** and confirm it settles onto the floor.
4. Drive and turn gently while the pallet is supported. Watch for excessive sliding, spinning, jitter, or penetration. Adjust the pallet's `LinearDamping`, `AngularDamping`, and mass in Details if needed.
5. Repeat with `AWoodPallet` if using the placement UI. Its root should report physical contact and respond to gravity after placement.
6. For an automatic pickup attempt, the status should only report completion after a recent fork contact and more than 2 cm of actual pallet rise. A fork that moves without lifting the pallet should fail instead of reporting a successful pickup.
7. Press **J** to create the side-by-side blue and orange cargo-box comparison pallets. Raise either pallet at least 5 cm; with collision debug enabled, confirm the load mass appears and the pallet stays in front of the backrest instead of clipping into the truck.

For diagnostics, enable the forklift's collision debug display. Yellow contact markers and the on-screen pallet support status help identify whether a tine is contacting the pallet. The runtime checks above have not been executed as part of this source change; they should be run in PIE on the target map.

## Current limits

This stage models rigid pallet/load contact and the effect of payload mass on the pallet's response. It does not yet model load-dependent forklift acceleration, vehicle suspension, tire forces, center-of-gravity transfer, or rollover. The chassis and fork actuators remain kinematic.

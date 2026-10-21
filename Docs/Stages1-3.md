# AGV implementation stages 1–3

This note records the changes after the Stage 0 baseline in `Stage0Baseline.md`. Existing manual controls, fork kinematics, pallet contact pickup, and collision sweeps remain in place.

## Stage 0 collision correction

`BodyCollision` is the swept chassis proxy. It was configured to ignore `ECC_PhysicsBody`, while both `APallet` and `AWoodPallet` use the `PhysicsActor` profile. That let pallet physics overlap the forklift's chassis proxy. The chassis now blocks `ECC_PhysicsBody`, and both pallet implementations explicitly block `ECC_Pawn`.

The two tines remain separate collision boxes. With the current dimensions, each tine starts 10 cm in front of the chassis proxy's forward edge. This leaves the fork insertion region outside the chassis sweep shape. Keep this clearance in mind if changing the dimensions or component offsets.

## Stage 1 — sensor contract and mount

- The body-fixed sensor remains attached to `BodyCollision`; it does not follow the lift/reach/fork carriage.
- A LiDAR snapshot now includes a timestamp, monotonically increasing scan sequence, sensor frame id, sensor-to-body transform, and per-ray status/range/point/direction.
- Rays remain in the sensor-local Unreal frame: X forward, Y right, Z up, centimetres.
- The current IMU values are derived from component world-transform differences. `bUsesGroundTruthKinematics` explicitly labels this ideal simulated source; do not treat it as an independently estimated pose or a physical sensor model.
- LiDAR traces use the Unreal `Visibility` channel. Actors/components that should be sensed must block that channel.

## Stage 2 — repeatable LiDAR measurement quality

`UForkliftNavigationSensorComponent` exposes optional range bias, Gaussian range noise, dropout probability, and a random seed under **LiDAR | Quality**. Noise is disabled by default to preserve the deterministic baseline. Enable it and set nonzero standard deviation/dropout to evaluate degraded scans. The same seed and scan sequence produce repeatable random draws when the hit pattern is the same.

Dropped returns are marked `Dropped` and invalid. They are not converted to free space. No-return rays remain distinct from dropped and self-occluded rays.

This is a configurable simulation model, not a claim that a particular real sensor has these exact error statistics. Tune its values against the intended hardware later.

## Stage 3 — local occupancy and obstacle observations

`UForkliftLocalOccupancyGridComponent` consumes only `FForkliftLidarScan`. Each new scan replaces the previous grid with a 2D snapshot centered on the body frame:

- cell values: `-1` unknown, `0` observed free, `100` occupied;
- valid external hits mark occupied endpoints and free cells along each ray;
- no-return rays mark observed free space up to sensor range;
- self-occluded and dropped rays contribute no cells;
- steep elevation rays and returns at the configured ground plane are filtered to reduce floor marks;
- neighboring occupied cells are grouped into body-frame obstacle observations.

The map is egocentric and single-scan. It does not accumulate across movement, estimate pose, classify obstacles as static/dynamic, or perform SLAM. `bDrawOccupiedCells` draws occupied cells for editor/PIE debugging. Visualization uses the actor transform only to display body-frame measurements in the world; that transform is not used to construct the grid or obstacle observations.

## Files added/changed for stages 0–3

- `Source/DigitalTwin/Forklift/AutomatedForklift.cpp` — block pallet physics on chassis; retain independent tines.
- `Source/DigitalTwin/Forklift/AutomatedForklift.h` — expose the per-forklift occupancy component.
- `Source/DigitalTwin/Forklift/Pallet.cpp`, `WoodPallet.cpp` — explicitly block pawn collision.
- `Source/DigitalTwin/Forklift/ForkliftNavigationSensorComponent.h/.cpp` — frame/sequence metadata, quality model, and explicit ideal-IMU label.
- `Source/DigitalTwin/Forklift/ForkliftLocalOccupancyGridComponent.h/.cpp` — local grid, hit clustering, and debug display.

## Validation and user-run checks

The UE 5.8.3 `DigitalTwinEditor Win64 Development` Live Coding compile completed successfully for the module. A normal full editor build was blocked while the editor held an active Live Coding session. No interactive PIE test was performed by this run.

After saving work and restarting the editor so the new reflected component is loaded, open `Map1` and verify:

1. Drive a simple pallet into the forklift chassis from the front, rear, and both sides; the pallet should stop at the chassis and not settle inside it.
2. Approach a pallet opening slowly; both tines should still enter without the chassis blocking the pallet.
3. Lift a correctly inserted pallet and verify contact-based support still raises it; repeat with a misaligned insertion and verify it does not report a successful lift.
4. Enable **LiDAR | Debug** and **Local occupancy | Debug > Draw Occupied Cells** on the forklift. Confirm external hits appear as occupied points while the scan frame id/sequence updates.
5. Enable **LiDAR | Quality**, use a fixed seed and nonzero range noise/dropout, then restart PIE twice. Compare the scan values and confirm dropped rays do not appear as free cells.

If a collision fails, enable **Drive debug > Enable Drive Debug** and **Draw Drive Debug** and record the pallet class/name, forklift name, contact direction, and Output Log hit target.

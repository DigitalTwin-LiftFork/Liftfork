# Stage 2 — load-dependent kinematic driving

Stage 2 keeps the chassis and fork axes kinematic. It adds a tunable vehicle response model driven by detected pallet mass (or `bIsLoaded` as a manual full-capacity override) and current fork lift stroke.

## Behavior

- Forward and reverse speed targets scale continuously from empty to the configured loaded limit according to detected payload mass divided by rated load (`LoadedMaxSpeedScale`, default 0.65 at rated capacity).
- Speed ramps toward the target instead of changing instantly. Loaded acceleration is lower than empty acceleration; loaded braking is also slower to represent a longer stopping distance.
- Steering response is reduced while loaded (`LoadedSteeringScale`, default 0.75).
- Raising the forks above `LiftHeightCautionStartCm` progressively reduces both travel speed and steering response, reaching the configured raised-fork scales at maximum lift.
- Fixed-obstacle sweeps continue to limit movement and fork-axis motion.

The defaults are simulation tuning estimates, not manufacturer performance data. No tire forces, suspension, chassis rigid-body motion, or rollover solver are added in this stage. A pallet is counted as a carried payload after fork contact and more than 5 cm of rise; its mass remains active while elevated over the truck. `bIsLoaded` in Details forces the full-capacity profile for comparison.

## Manual verification in Unreal Editor

Open `Map1` and enter Play. Press **J** to spawn a blue 250 kg total pallet and an orange 1,450 kg total pallet, each with a visible collision box cargo load. Lift one above 5 cm; the HUD reports pallet, cargo, and total mass separately. Press **K** to show the on-screen drive diagnostics. The two pallets spawn side by side in front of the truck, so steer to line the forks up with each. Set the fork down before lifting the other. For an empty/full-capacity profile comparison, stop PIE, select the forklift, and toggle `R 16 HD > bIsLoaded` in Details between runs.

1. On level ground with forks lowered, lift the blue pallet and check that the HUD reports pallet 50 kg, cargo 200 kg, total about 250 kg. Drive forward and note its target speed and acceleration.
2. Set it down, pick up the orange pallet and confirm the HUD reports pallet 50 kg, cargo 1,400 kg, total about 1,450 kg. Repeat the same drive input; its target speed should be lower and acceleration slower than with the blue pallet.
3. Release W or S from a steady speed, then compare stopping time. The heavier load should take longer to stop.
4. At the same lift height, hold A or D for the same duration with each box. The heavier load should produce a smaller yaw change.
5. Repeat with the forks raised. Speed and steering response should reduce progressively above `LiftHeightCautionStartCm`.
6. Confirm obstacle collision sweeps still stop motion at fixed geometry in both profiles.

The on-screen status continuously shows actual speed and target speed in km/h plus detected payload mass in kg; it does not require collision debug. Press **K** for the more detailed collision and fork status, including fork height. Runtime PIE checks have not been executed as part of this source change; compare these behaviors in the editor and tune the exposed values to suit the intended simulation.

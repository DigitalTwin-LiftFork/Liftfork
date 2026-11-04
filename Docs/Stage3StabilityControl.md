# Stage 3 — load and turning stability control

Stage 3 adds an analytical stability guard to the kinematic forklift driving model. It estimates lateral acceleration from current speed and steering demand, then compares that estimate with a conservative limit adjusted for payload mass and fork lift height.

## Behavior

- `UnloadedLateralLimitG` and `LoadedLateralLimitG` define the estimated lateral acceleration envelope. The defaults (0.35 g unloaded and 0.18 g at rated load) are simulation tuning values, not manufacturer data.
- `RaisedForkLateralLimitScale` progressively tightens the envelope as the forks rise above the Stage 2 caution height.
- As the estimated risk approaches the envelope, the controller progressively reduces target travel speed and steering response. It bottoms out at `StabilityControlMinimumScale` rather than abruptly stopping the truck.
- The HUD reports `STABILITY` (`NORMAL`, `CAUTION`, or `CONTROL ACTIVE`), estimated lateral acceleration in g, and the normalized risk ratio. A risk ratio of 1.0 means the estimated lateral acceleration reached the configured envelope.
- Stage 2 load-based speed, acceleration, braking and fork-height controls remain active.
- The kinematic chassis listens for body impacts from moving actors and simulating bodies. It estimates transferred momentum from the pusher's mass and incoming speed, moves the truck with that added velocity, then damps the drift over time. `ForkliftEffectiveMassKg`, `DefaultPusherMassKg`, `ImpactMomentumTransferScale`, `PushVelocityDecayCmPerSecSq`, and `MaximumImpactPushSpeedCmPerSec` tune this response.

The current chassis and fork carriage remain kinematic, and the pallet remains a separate Chaos rigid body. The impact response is an estimated momentum transfer layered on the kinematic chassis; it is not a full chassis rigid-body simulation. The stability guard reduces risky commands; it does not simulate chassis roll, wheel lift, tire forces, suspension, or a physical rollover event. Calibrate the editable values against measured data before using this model for engineering conclusions.

## Manual verification in Unreal Editor

Open `Map1` in PIE and press **J** to spawn the 250 kg blue and 1,450 kg orange comparison pallets. Lift and carry each pallet at the same speed and fork height, then steer with **A/D**:

1. The HUD should show lateral acceleration, a risk ratio, and a stability label.
2. With the heavier pallet, or with the forks raised above the caution height, the same turn should reach a higher risk ratio.
3. As risk approaches 1.0, target speed and steering response should reduce smoothly. Lowering the forks or straightening the wheels should let the control return toward normal.
4. Compare unloaded, light-load, heavy-load, and raised-fork behavior; tune the exposed `R16 HD | Stability control` values if the intervention feels too strong or too weak.
5. Move or throw a physics-enabled pallet into the side/rear of the stationary forklift. The chassis should drift in the impact direction, then slow to rest; `EXTERNAL PUSH` in the HUD should rise and decay to zero. Press **K** and inspect `[R16HD_IMPACT_INERTIA]` in Output Log for the estimated pusher mass and transferred speed.

PIE runtime behavior has not been verified as part of this source change. These values are intended to demonstrate a tunable safety response, not certify forklift stability.

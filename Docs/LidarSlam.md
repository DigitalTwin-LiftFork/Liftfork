# LiDAR / 2D local SLAM

Open DigitalTwin.uproject in this folder, open Factory and press Play.

- W/S, A/D: existing manual driving.
- L: show/hide LiDAR ray debug.
- M: show/hide the SLAM map (visible by default).
- B: clear map and initialize a new local coordinate frame at the current sensor position. Stop before resetting.
- The former H-key low-speed obstacle avoidance has been removed.

The existing 360-degree multi-layer LiDAR now has an additional horizontal ring (360 rays at the default settings). Simulated rays use Visibility collision, include self-occlusion, and report sensor-local measurements. Objects must block Visibility to be measured. Range defaults to 30 m (legacy 15 m instances are migrated at BeginPlay); scan interval defaults to 0.1 seconds.

ForkliftSlamComponent uses horizontal returns and the IMU angular rate for planar scan-to-map matching. It does not read actor world pose or the simulated IMU's ideal linear velocity. Translation is estimated from matching and previous estimated velocity. The estimated pose refers to the LiDAR, with the first accepted scan defining the local origin. Green cells are obstacles, grey cells observed free space, dark cells unknown; the yellow marker is the estimated sensor pose. The view is a 30 m square centered on that pose, with fixed map orientation. Grid resolution is 20 cm and map bounds are +/-100 m from the initial pose. Match percentage is a scan agreement score, not a calibrated localization probability.

Insufficient returns or poor agreement skip that map update. Every new scan retries matching against the last accepted map; a single missed or delayed scan no longer permanently freezes SLAM. The LIVE LiDAR panel uses sensor-local points independently of mapping. Scans and Map updates counters distinguish active sensing from successful localization. Long tracking losses can still require stopping and resetting with B; automatic global relocalization is not implemented. Move slowly with manual controls while observing alignment. This is a local SLAM prototype, not a complete navigation system: no loop closure, global relocalization, map persistence, object classification in this component. Destination navigation is documented separately in GoalNavigation.md. Repeated shelving and featureless corridors can cause drift or ambiguous matches even with a high score. Floor ramps and tilted sensor operation are outside the planar model. Empty/no-return beams are left unknown rather than assumed safe. Existing pallet handling remains available independently.

Implementation is compiled with UE 5.8; interactive Play behavior and localization accuracy require runtime evaluation.
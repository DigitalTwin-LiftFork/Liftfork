# Automated Forklift (C++ only)

`AAutomatedForklift` is a C++ reach-truck proxy based on **R 16 HD automated config. 4**. Unreal uses centimetres: mm / 10 and m/s * 100. The single source of editable geometry and travel limits is `Dimensions`; all rates and turning radii are in `Motion`.

The supplied source values are present in those two structs: 265.9 cm overall length, 129/157 cm widths, 145.3 cm wheelbase, 180.8 cm automatic front radius, 113.7/118.4/118.4 cm rear radii, travel 195/75 cm/s, lift/lower 52/66 and 55/44 cm/s, reach 39.3 cm at 20 cm/s, sideshift +/- 8 cm at 10 cm/s, two 4.5 x 10 x 115 cm forks, and the 1,600 kg / 60 cm rated-load specification.

`BodyHeightCm`, wheel dimensions, mast width, steering rate, and maximum lift height are marked **TEMP** because they were not included in the supplied table. The latter defaults to 500 cm and must be confirmed from the drawing.

Visual components have `ReplaceMesh` in their names and have no collision. `BodyCollision` and `ForkCollision` are independent collision proxies, while `MastAssembly`, `LiftCarriage`, `ReachCarriage`, and `ForkCarriage` retain kinematic placement. Therefore changing any visual Static Mesh in the Details panel preserves the dimensions, collision and controls.

Map1 uses the C++ `ADigitalTwinGameMode` and `AForkliftPlayerController`. At Play In Editor, the game mode finds a placed `AAutomatedForklift` first and possesses it for Player 0. If none exists, it creates one at the Player Start (or origin) with its collision bottom 5 cm above the floor, then possesses it. The standard template pawn is disabled. The pawn has a C++ rear three-quarter spring-arm camera that follows it. Controls: W/S drive, A/D steer, E/Q lift/lower, R/F reach out/in, and Z/C fork sideshift. No Blueprint setup is required.

Hold the right mouse button and move the mouse to orbit the camera without rotating the truck. Use the mouse wheel to zoom and Home to restore the rear three-quarter view. Pitch is limited to -65 through -10 degrees and distance to 350 through 1,000 cm. Enable **Drive debug > Enable Drive Debug** on a placed forklift to log input, requested/current speed, locations, blocking hit target, start penetration and initial overlaps; enable **Draw Drive Debug** to see the attempted movement arrow and blocking-hit marker.

Supported loads now reduce travel speed as fork height and reach extension increase. The defaults in **Load safety** are simulation tuning values, not manufacturer stability data. Loads above the configured 1,600 kg rated capacity trigger an overload status, limit travel speed, and block further lifting or reach extension while still allowing the operator to lower and retract the load.

Drive speed now ramps using configurable acceleration and braking, and steering yaw rate follows a speed-dependent bicycle model with a ground-friction cap. With a supported load, the simulation estimates the combined center of mass and its inertial shift against the three-wheel support triangle; a small margin reduces speed, and a negative margin brakes drive/steering and blocks further lift/reach-out while allowing lowering and retraction. This remains a kinematic planar vehicle model: it does not simulate suspension, wheel spin, chassis roll, or a full Chaos rigid-body tip-over.

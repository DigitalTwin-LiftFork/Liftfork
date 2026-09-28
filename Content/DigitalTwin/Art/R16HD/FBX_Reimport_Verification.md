# R16 HD FBX re-import verification

All bounds are Blender metres after importing the FBXs Blender exported.

| FBX | Mesh objects | Bounds X x Y x Z (m) |
| --- | ---: | --- |
| `Body_Actual.fbx` | 14 | 2.6600 x 1.2900 x 1.8950 |
| `DriveSteerWheel_Actual.fbx` | 3 | 0.3600 x 0.1418 x 0.3600 |
| `LoadWheel_Actual.fbx` | 3 | 0.2850 x 0.1100 x 0.2850 |
| `Underframe_Actual.fbx` | 12 | 2.4150 x 1.5600 x 0.3550 |
| `MastOuter_Actual.fbx` | 5 | 0.1200 x 0.9000 x 5.0000 |
| `MastInner_Actual.fbx` | 5 | 0.0800 x 0.7000 x 4.5000 |
| `MastLift_Actual.fbx` | 5 | 0.0500 x 0.5500 x 3.8000 |
| `ReachCarriage_Actual.fbx` | 4 | 0.7550 x 0.7200 x 0.2600 |
| `ForkCarriage_Actual.fbx` | 5 | 0.2450 x 0.9500 x 0.4400 |
| `ForkLeft_Actual.fbx` | 2 | 1.1500 x 0.1000 x 0.4725 |
| `ForkRight_Actual.fbx` | 2 | 1.1500 x 0.1000 x 0.4725 |
| `Body_UE_Normalized.fbx` | 14 | 1.0004 x 1.0000 x 1.9947 |
| `MastOuter_UE_Normalized.fbx` | 5 | 1.0000 x 1.0000 x 1.0000 |
| `MastInner_UE_Normalized.fbx` | 5 | 1.0000 x 1.0000 x 1.0000 |
| `MastLift_UE_Normalized.fbx` | 5 | 1.0000 x 1.0000 x 1.0000 |
| `ForkLeft_UE_Normalized.fbx` | 2 | 1.0000 x 1.0000 x 10.5000 |
| `ForkRight_UE_Normalized.fbx` | 2 | 1.0000 x 1.0000 x 10.5000 |

## Master assembly wheel / ground check

| Wheel object | Diameter x width (m) | Lowest world Z (m) |
| --- | --- | --- |
| `DriveWheel_Tire` | 0.3600 x 0.1300 | -0.0000 |
| `LoadWheelLeft_Tire.001` | 0.2850 x 0.1000 | -0.0000 |
| `LoadWheelRight_Tire.001` | 0.2850 x 0.1000 | -0.0000 |

| Frame check | Lowest world Z (m) |
| --- | ---: |
| `Underframe_MastLowerSill` | 0.0400 |
| `Underframe_CenterUnderframe` | 0.0440 |

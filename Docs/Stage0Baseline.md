# Stage 0 — AGV forklift baseline

This document records the existing manual-control and collision behavior before autonomous navigation work. Stage 0 does not add navigation, SLAM, perception, or new vehicle behavior.

## Build and project baseline

- Project: `DigitalTwin.uproject`, engine association `5.8`.
- Engine available on the development PC when recorded: Unreal Engine `5.8.3`.
- C++ editor target: `DigitalTwinEditor`, Win64, Development.
- Forklift class in the current source: `AAutomatedForklift` (`APawn`). The earlier design notes called it `AAGVForklift`; that class is not present in this checkout.
- The playable test level is `Content/DigitalTwin/Map1.umap`.
- `Config/DefaultEngine.ini` currently names `/Game/TopDown/Lvl_TopDown` as both the game default and editor startup map. Open `Map1` explicitly for the forklift baseline; this stage intentionally does not change the project's default map.
- `ADigitalTwinGameMode` disables the template default pawn, adopts a placed `AAutomatedForklift` when present, or spawns one at a PlayerStart/origin and possesses it for Player 0.

## Existing behavior to preserve

### Manual controls

| Input | Existing action |
|---|---|
| W / S | Forward / reverse |
| A / D | Steer |
| E / Q | Raise / lower forks |
| R / F | Extend / retract reach |
| Z / C | Side shift |
| Hold right mouse + move | Orbit the follow camera |
| Mouse wheel | Zoom camera |
| Home | Reset camera |
| K | Toggle collision debug |
| L | Toggle LiDAR-ray debug (diagnostic only; not SLAM) |
| P | Spawn the simple test pallet |
| G | Logs that the old attachment path is disabled; pallet lifting is contact-based |

Input mappings are in `Config/DefaultInput.ini`; axis handlers are bound in `AAutomatedForklift::SetupPlayerInputComponent`.

### Collision and fork mechanism

- Visual meshes are separate from collision proxies. The body, left/right tine, support-leg, and carried-pallet shapes participate in movement checks independently.
- Chassis movement uses a swept actor offset. Support legs and forks are swept before the body move; the earliest blocking hit limits that frame's requested translation.
- Steering checks short intermediate yaw steps against fixed obstacles for the tines and support legs.
- Lift, reach, and side shift use kinematic controls with fork-axis fixed-obstacle sweeps.
- Initial overlaps allow escape/tangential movement and block motion deeper into the overlap.
- When carrying a pallet, its collision shapes are included in external-obstacle sweeps.
- The wooden/test pallets use physical fork contact; automatic lift verification checks that the pallet actually rose. `G` must not be assumed to attach a pallet by teleporting it.
- Existing collision diagnostics can be enabled with **Drive debug > Enable Drive Debug**, **Draw Drive Debug**, and the collision-debug toggle. Relevant log prefixes include `R16HD_COLLISION_FIX_V2` and `R16HD_KINEMATIC_FORK_V7`.

## Stage 0 manual regression checklist

Run in UE 5.8.3 using `Map1` and Play In Editor. Keep this checklist as the pre-change comparison for later stages.

1. **Startup/possession:** confirm the forklift is possessed and accepts input. With a placed forklift, confirm the game mode uses it instead of spawning a duplicate.
2. **Drive and steering:** test W/S and A/D in open space. Confirm forward and reverse both move, steering changes heading, and releasing input stops further commanded movement.
3. **Fork axes:** test E/Q, R/F, and Z/C separately. Confirm each moves its intended assembly and does not move the camera or chassis unexpectedly.
4. **Camera:** orbit while holding the right mouse button, release it and verify the truck no longer orbits with the mouse, zoom with the wheel, and reset with Home.
5. **Body collision:** approach a static wall slowly; the body should stop at contact without passing through or being visibly displaced through the wall.
6. **Fork/support collision:** with forks raised or extended, approach a static obstacle using the tines and support legs. Confirm the blocked component limits chassis motion and collision diagnostics identify the obstacle.
7. **Initial-overlap recovery:** begin with a small overlap (or reproduce one from the map). Confirm movement out of the overlap is possible and movement deeper into it is blocked.
8. **Pallet contact:** spawn/place a test pallet, approach its openings, insert both tines, and lift slowly. Confirm the pallet rises from physical support and that a partial/misaligned insertion does not report a successful lift.
9. **Carried-load clearance:** while supporting a pallet, approach a fixed obstacle with the load. Confirm the load sweep blocks motion before the pallet passes through the obstacle.
10. **Repeatability:** restart PIE and repeat the same drive/collision case; confirm the result and diagnostic hit target are consistent.

Record pass/fail, the map and actor names, and any relevant Output Log lines for each item. A failed item is a baseline defect to diagnose before Stage 1; it is not a reason to work around or remove the existing collision code.

## Validation record

- UE 5.8.3 `DigitalTwinEditor Win64 Development` build: **Succeeded**; UnrealBuildTool reported the target up to date.
- `git diff --check`: **Passed**.
- Interactive Play In Editor checklist: **Pending user run**. The current workspace run did not drive the editor UI, so movement, collisions, and physical pallet contact are not claimed as runtime-verified by this record.
- Toolchain note: this PC reports MSVC 14.51 as newer than UE 5.8's preferred 14.50 toolchain. This was a warning; the build succeeded.

## Known limits at baseline

- The test checklist requires interactive Unreal Editor validation. A successful C++ build alone does not verify input, contact behavior, or map setup.
- `Map1` is not currently configured as the game's default map; it must be opened explicitly for this checklist.
- The existing LiDAR component is a ray-trace simulation/debug sensor. It does not provide a map, localization, SLAM, path planning, or obstacle avoidance.
- Existing pallet image processing is a local insertion-opening experiment. It is not general pallet/object detection.
- Manufacturer dimensions and simulation assumptions are distinguished in `Docs/AutomatedForklift.md` and the actor's editable properties; keep that distinction in future comparisons.

## Follow-up collision finding

The Stage 0 manual checklist had not yet been run when the baseline was recorded. During the next implementation pass, source inspection found that the chassis ignored `ECC_PhysicsBody`, even though both pallet classes use `PhysicsActor`. The collision response was corrected in the stages 1–3 implementation. See `Docs/Stages1-3.md` for the change and required PIE regression checks; do not treat the runtime checklist as passed until it is exercised in the editor.

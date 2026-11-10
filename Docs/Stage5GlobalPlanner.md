# Stage 5 — global path planning

## Scope

`UForkliftGlobalPathPlannerComponent` uses only the per-forklift Stage 4 SLAM occupancy map and estimated pose. It runs an 8-connected A* search from the estimated map pose to an editable goal pose. Cells classified as occupied or unknown are blocked; only cells classified as observed free are traversable. Diagonal moves cannot cut across blocked corners.

The planner inflates blocked cells using a conservative circular envelope derived from half the forklift length and maximum width, plus `SafetyMarginCm`. A turn penalty biases A* toward routes with fewer/shallow direction changes. This is a global grid route and conservative footprint check; it does not guarantee a dynamically feasible steering curve. Exact trajectory tracking and live obstacle reactions remain Stage 6.

The component exposes the goal position/yaw, path waypoints, status and expanded-node count. It retries once per second as new scans arrive when `bPlanOnBeginPlay` is enabled, but requires a tracking-valid SLAM pose and an observed-free goal. Cyan lines show the planned route through `MapToWorldForDebug`; a yellow marker and arrow show the goal pose. The planner never reads the forklift actor transform to estimate the start pose and never sends drive commands.

## Test in UE 5.8.3

1. Rebuild `DigitalTwinEditor Win64 Development`, restart the editor, open `Map1` and start PIE.
2. Select the spawned forklift and inspect its `GlobalPathPlanner` component. Set `GoalMapCm` to a point in the scanned, open area ahead of the initialized SLAM origin; leave `bPlanOnBeginPlay` enabled.
3. Wait for `PlannerStatus` to report `Path ready`. Inspect `PlannedPath` and `ExpandedNodeCount`; cyan route segments and the yellow goal marker should appear in the debug map frame.
4. Set a goal inside a mapped occupied cell or outside the SLAM map. The planner should report no route or out-of-map status and clear the old route.
5. Verify that the forklift does not move by itself. This stage only computes and draws a route.

If status says `Waiting: SLAM pose is not tracking-valid`, Stage 4 localization has not supplied a valid pose. If it says no route through known free space, check whether the map has observed-free cells and enough clearance for the forklift envelope. The current project logs have previously shown `mapCells=0`; in that state a successful route cannot be expected until the Stage 4 scan/map issue is resolved.

## Build and validation

- UE 5.8.3 `DigitalTwinEditor Win64 Development` compile and DLL link: **Succeeded**. A follow-up UBT invocation reported `Target is up to date`.
- UnrealBuildAccelerator initially attempted to write under `C:\ProgramData`; setting `UBA_ROOT` to the writable workspace build directory resolved the access-denied setup issue.
- Compiler emitted existing UE deprecation warnings and the installed MSVC 14.51 newer-than-preferred 14.50 warning; no Stage 5 compile errors remained.
- Interactive PIE route-generation checks: require the user's scene to produce a tracking-valid SLAM pose and observed-free cells; not implied by a successful compile.

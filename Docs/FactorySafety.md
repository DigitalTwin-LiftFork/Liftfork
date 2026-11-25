# Factory traffic safety prototype

This prototype adds a conservative traffic interlock to the single forklift. It is intended for simulation and classroom testing; it is not a certified industrial safety system.

## Controls

- `T`: spawn or remove a slow crossing test cart in front of the forklift. The cart travels across the forklift's path and reverses at the end of its route.
- `X`: latch or release the emergency stop. While latched, drive, steering, lift, reach, and sideshift commands are held at zero. Press `X` again to release it.
- `W/S`: approach or retreat from the cart. If a dynamic obstacle or another pawn enters the predicted travel envelope, the forklift brakes to a stop and yields. It resumes only after the travel envelope clears and the operator still holds a drive command.

The cart is a visible 2.4 m by 1.3 m by 2.0 m proxy, not a person model. It blocks the forklift and LiDAR Visibility channel. Its path and speed can be adjusted in the `Traffic test` properties after selecting it during PIE.

## Safety zones

Place `ForkliftSafetyZone` actors in a level. Resize and rotate the box volume in the editor, then choose:

- `Speed limit`: caps travel speed within the zone to `Maximum Speed Cm Per Sec`.
- `No entry`: requests a stop before the forklift's conservative footprint reaches the zone.

Speed-limit zones are cyan and no-entry zones red when `Draw Debug Zone` is enabled. The runtime safety state is shown in the lower-right navigation diagnostics and logged with the `[FACTORY_SAFETY]` prefix.

## Manual check in PIE

1. Open the `Factory` map and start Play.
2. Press `T`; verify that the cart appears ahead and moves laterally across the route.
3. Hold `W` toward it. Verify that speed falls to zero before contact, the diagnostics report `통행 양보 정지`, and the forklift stays stopped while the cart crosses.
4. Keep holding `W`; once the cart leaves the safety envelope, verify that the forklift resumes. Press `T` to remove the cart.
5. Press `X` while moving. Verify that the forklift brakes and the HUD/log reports emergency stop. Press `X` again to release it.
6. For zone checks, stop PIE, place a `ForkliftSafetyZone`, set its type, and start PIE again. Approach a cyan speed-limit zone and then a red no-entry zone; verify the speed cap and pre-entry stop in the diagnostic state.

The dynamic-object interlock queries collision volumes independently of LiDAR. The existing autonomous navigation also evaluates its LiDAR obstacle samples and simulation collision guard. A dynamic obstacle has conservative right-of-way: the forklift yields rather than negotiating a reservation with it. Dynamic traffic path coordination, time-based intersection reservations, pedestrian animation, and sensor-only perception are not part of this prototype.

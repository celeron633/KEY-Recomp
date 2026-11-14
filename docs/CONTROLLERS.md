# Controller and settings guide

## Controls

Open the native settings overlay with **F1**; close with F1, Escape, or Close. **F2** toggles the FPS overlay and saves the settings. Other edits apply immediately; use **Save settings** to retain them in `private/user/controls.ini`. Invalid or incomplete settings files produce a diagnostic and fall back to defaults without overwriting the file until Save is requested.

The settings menu blocks gameplay input. It does **not** suspend the game, audio, or guest clock. Pause with Wii Plus / DS4 Options before opening it when necessary. Buttons held when closing settings must be released before controller gameplay input resumes.

| DualShock 4 default | Emulated input |
| --- | --- |
| D-pad / left stick | Sideways Wii Remote directions |
| Cross | Wii 2 / jump / confirm |
| Square | Wii 1 / yarn / back |
| Circle / Triangle | Wii A / B |
| Options / Share / PS | Wii Plus / Minus / Home |
| R3 | Recenter pointer |

Click a button binding in F1, release held buttons, then press the desired controller button. Reusing an assigned button swaps its previous assignment. Direction controls remain D-pad/left stick; the stick deadzone is configurable. The current configuration UI targets player 1; two-player configuration and motion have not been validated.

Keyboard controls remain arrows/WASD, X, Z, Enter, C, and Space. **Q / E** supply opposite sideways tilt values, including the tank-style interaction in the owner's screenshot. **R** recenters the gyro/right-stick pointer. Select **Mouse** as the pointer source for keyboard/mouse play.

## Bluetooth motion

Pair the physical DualShock 4 with Windows before selecting it in F1. The menu displays the assigned device and whether SDL can enable its gyroscope and accelerometer. An Xbox-emulated device may not expose those sensors. Device selection uses Aurora's existing persistent player assignment; motion configuration is stored separately in `controls.ini`.

- **Tilt:** the accelerometer, including gravity, is filtered and rotated into sideways Wii Remote axes. Tilt sensitivity and Reverse tilt affect this path.
- **Pointer:** gyro angular velocity moves a relative virtual IR pointer. Mouse, right stick, and Disabled are alternative sources. Pointer sensitivity, deadzone, and Reverse pointer X/Y are configurable. Mouse coordinates are unaffected by inversion.
- **Calibration:** keep the controller still for two seconds after pressing Calibrate gyro. Movement restarts the stationary interval. Calibration estimates gyro bias, lasts for the current connection, and resets on device replacement/disconnect. It does not zero the physical tilt angle.
- **Recenter:** R / R3 returns the gyro/right-stick pointer to the center. Gyro aiming has no physical sensor-bar reference and may need recentering over time.

The default gyro horizontal and vertical signs were reversed on 2026-11-14 in response to the owner's report that both directions were reversed. Existing inversion flags remain user preferences relative to this corrected baseline; right-stick and tilt defaults were not reversed by that correction.

## Implementation and verification

`controller/` owns the SDL controls, settings overlay, motion conversion, and guest WPAD service bridge. Its CMake object target is linked into the existing graphics runtime. The renderer only initializes, polls, draws, and shuts down the controller module; no reference checkout was modified and no new dependency was imported.

Input remains SDL -> host WPAD -> big-endian guest WPAD reports -> original KPAD. A routing layer suppresses Aurora's default physical player-1 mapping before supplying the custom mapping, avoiding duplicate Wii actions. Focus loss and menu capture neutralize input; sensor-less or disconnected gyro pointers are invalid. Integration rejects nonfinite samples and host intervals over 100 ms instead of integrating a large stale rotation.

`controller_motion` in CTest checks gyro signs, sampling-rate independence, inversion, gravity axes, calibration, bounds, and stall rejection. `private/build/graphics/controller/input_smoke.exe` additionally uses a synthetic SDL controller to check buttons, gravity and IR through the guest ABI, capture/release, disconnect/focus, keyboard tilt, settings replacement/reload, and malformed settings rejection. This smoke check requires the prepared graphics DLL/toolchain PATH; it writes isolated evidence beneath `private/reports/`.

Directly observed: the local PS4 controller exposed both sensors, the F1 menu rendered, and an FPS overlay appeared at the title. After the direction correction, the owner reported that the basic functions work and no further bugs were apparent. The exact physical test sequence was not independently recorded; synthetic tests and this owner report do not establish Bluetooth latency, full-game motion coverage, or full-controller compatibility.

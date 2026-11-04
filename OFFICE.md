# Main-monitor office ambience

The office path is opt-in: `office-ambience.json` in OpenRGB's private configuration
directory activates it. Without this file, the upstream desktop capture/settings
path remains available. The office path requires the verified six-controller,
314-LED inventory. The lighting owner remains headless. `OfficeControl.exe` is
an optional native Qt panel; it neither captures nor submits RGB itself.

```json
{"monitor":"\\\\.\\DISPLAY2","fps":15,"smoothing_ms":140}
```

Window-state events choose a maximized/fullscreen application on that monitor.
Otherwise WGC captures the top-level shell host containing Wallpaper Engine's
renderer and crops only that monitor on the GPU. The child renderer is not a
capturable WGC target on the tested machine. There is no composed-desktop fallback
for wallpaper capture. A nonempty visible desktop-icon list is rejected because
its exclusion is not verified. The user's tested setup has no desktop icons.

GPU region sampling reads back one RGBA pixel per physical LED. Capture callbacks
replace one current colour row; there is no frame queue. The output timer applies
temporal smoothing and updates the six native controllers. While sources change
or capture is unavailable, it retains the previous valid row. Black imagery is
valid. The accepted approximate perimeter layout is 3/7/4/6, and bar order is left
ten/right ten. Laptop lighting follows the LG in this release.

`office-ambience-status.json` is atomically updated in the private configuration
directory. It contains source mode/HWND, input/output counters, processing time,
first-frame delay and capture errors, without screenshots or access tokens.

Build against the matching OpenRGB API-5 headers and Qt 6.8.3 with
`scripts/Build-Office.ps1`. The Windows workflow pins the native source revision.
The production D3D11 shader is exercised using WARP by `office_capture_tests`;
actual HWND capture and controller output still require the real office setup.

## Native control panel

Use the delivered `office/Open-Office-Control.ps1` launcher. Control offers
enable, output FPS, smoothing, spatial calibration and return to ambience.
Topology shows the actual downscaled LG capture, per-LED sampling rectangles,
hardware LED names/indices, and current output colours. Select a device/zone or
one LED; drag, resize, quarter-turn or reverse its sampling order. Coordinates
are percentages of the LG image, with Y increasing downwards. Devices includes
per-controller enable and a two-second identify. Status exposes capture errors,
frame age separately from new-frame submission latency, and explicit recovery.

Changes stay in the panel draft until Save. Save validates the complete map and
hardware identities, rejects a stale revision or external file edit, prepares
the GPU buffer, then atomically persists and applies it. Reload discards the
draft. The default map is the accepted existing map; opening the panel does not
move it. The private `office-control.json` contains a per-process authenticated
named-pipe endpoint. Never publish this file. The pipe allows only the same user;
it offers fixed JSON commands, with no shell or code evaluation. Disconnecting
returns any active calibration to ambience.

The preview is requested only while a panel is connected, downsampled on the GPU
and read back at a small size. Normal capture still reads only the LED row.
Capture recovery is driven by source/window, display, resume and session events;
an unchanged unavailable source can be retried explicitly. No timed reset loop.

The panel's dark cyan hierarchy and topology interactions were reconstructed
from EDAmbilight v1.2.0 by Stefano Indraccolo. Its MIT credit is delivered as
`LICENSE-EDAmbilight-MIT.txt`; see `EDAMBILIGHT-GUI-REVIEW.md` for shipped evidence.
Unsupported EDA algorithms and synthetic previews are not presented as native
capture features. All six office controllers, including keyboards, are editable.

This fork is under active integration. A successful build alone is not physical
acceptance or final delivery. The core integration specification owns those gates.

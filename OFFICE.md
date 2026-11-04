# Main-monitor office ambience

The office path is opt-in: `office-ambience.json` in OpenRGB's private configuration
directory activates it. Without this file, the upstream desktop capture/settings
path remains available. The office path requires the verified six-controller,
314-LED inventory and constructs no application window.

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

This fork is under active integration. A successful build alone is not physical
acceptance or final delivery. The core integration specification owns those gates.

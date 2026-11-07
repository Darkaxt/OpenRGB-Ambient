# Main-monitor office ambience

Left-click the tray icon for the lighting mixer. Its top slider sets all mapped
lights together; each physical device has its own slider and mute/restore icon.
Offline devices keep their settings and use them when reconnected. Levels save
immediately. The pin keeps the flyout open; the chevron collapses the controls.
Escape or clicking elsewhere closes an unpinned flyout. Double-click or Open
opens the full panel; its Output card also has a Lighting mixer button.

Fullscreen intensity is a single percentage shared by all fullscreen apps on the
main display (default50%). It multiplies master/device intensity and the existing
brightness baseline/display-follow factor; exiting fullscreen restores normal
levels. Ordinary maximized windows and other displays do not activate this dimming.
Master/device defaults are100%;0 mutes. Master mute releases image capture;
screen adjustment and Awake remain independent. Calibration/identify reference
colours and the existing pause/rest off policy are preserved.

The mixer adapts BetterTrumpet's accented header, icon/slider/value rows, pinning,
collapse and dismissal interactions to light-emitting devices. It adds no sound,
per-app profiles, external control process or second RGB writer. A pending draft
in the full editor is retained; reload before saving if immediate slider changes
made that draft's saved revision stale.

Capture always follows the display marked as main in Windows. The primary flag,
device name and current bounds are resolved again on source selection and display
change/resume events; no DISPLAY number or physical LG identity is pinned. Legacy
saved monitor names are ignored without rewriting saved topology/settings on load.
The control snapshot reports the effective monitor policy as `primary`; a later
explicit Save persists that policy alongside the existing settings. Diagnostics
include `capture_display` and `capture_bounds` for the actual selected display.
Undocking naturally selects the laptop's main display. Disconnected controllers
retain their maps; connected laptop/mouse lighting continues. Regions remain
normalized across resolutions, so their relative placement is preserved.

Identical captured main-display pixels are rejected by an exact GPU comparison before mip
generation, blur, media-border detection or colour extraction. Only a four-byte
change flag reaches the CPU; the full image stays on the GPU. Other-monitor pixel
changes do not invalidate the main-display crop. New dimensions, sources, mapping and image
settings still invalidate their affected work. Static previews reuse the encoded
generation rather than recompressing an identical screenshot.

The output clock continues pending colour adaptation, but writes only changed
LED buffers and submits only changed devices. A device reconnect receives the
complete retained state even if it is black. Device protocols may batch a whole
controller frame; per-LED API calls that merely repeat that frame are avoided.
The Govee driver's independent slow cached-colour keepalive remains in place;
it does not repeat image processing or re-enter streaming on H6609/H8048.
Diagnostics distinguish capture callbacks, changed/skipped inputs, sampling
passes, output-clock ticks, LED writes and native device submissions. An output
clock rate is not a claim that static hardware receives that many fresh frames.

The office path is opt-in: `office-ambience.json` in OpenRGB's private configuration
directory activates it. Without this file, the upstream desktop capture/settings
path remains available. The office path requires the verified six-controller,
314-LED inventory. The lighting owner remains headless. `OfficeControl.exe` is
an optional native Qt panel; it neither captures nor submits RGB itself.

```json
{"monitor":"primary","fps":15,"smoothing_ms":140}
```

Window-state events choose a maximized/fullscreen application on that monitor.
An automatically activated Windows idle screensaver rests all lights and releases
capture. Dismissing it resumes from fresh imagery. The Windows saver-running flag
or `Screen-saver` input desktop distinguishes this from a shortcut launch.
A manually launched fullscreen main-display screensaver on the ordinary input desktop
takes capture priority. Desktop-switch events restore the normal policy when
an automatic saver exits; the owner never switches the user's desktop.
Manually started fullscreen `.scr`/`WindowsScreenSaverClass` windows are recognized
on the ordinary desktop too, including the Ctrl+Alt+L Wallpaper Engine saver
whose manual launch leaves Windows' screensaver-running flag unset.
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
ten/right ten. All connected lighting follows the Windows main display.

Windows session lock, session display sleep, an inaccessible/secure input desktop,
or the absence of a Windows main display forces all output black and releases capture. This takes
priority over identify, calibration and the standby preview. Unlock/display wake
recreates capture and waits for fresh imagery; saved mapping/brightness are not
modified. A dimmed display is still awake. Session display notifications do not
prove that an independently switched-off physical panel is asleep when Windows
continues reporting it as connected and awake. Diagnostics expose `rest_reason`
and `input_desktop` without capturing secure-desktop imagery.

`office-ambience-status.json` is atomically updated in the private configuration
directory. It contains source mode/HWND, input/output counters, processing time,
first-frame delay and capture errors, without screenshots or access tokens.

`logs/office-ambience-events.jsonl` independently retains lifecycle history:
UTC plus monotonic time, PID/TID, per-process session/sequence and source commit.
It records capture create/start/release intent and completion, source HWND/PID,
process basename/class (no window titles), desktop/lock/display events, first
frames, HRESULT/device-removal errors, control actions and device availability.
Health summaries are appended every ten seconds; no per-frame records or pixels
are logged. Writes use Windows write-through with no application flush timer;
completed records survive abrupt process exit. Retention is eight 4 MiB segments
(current plus .1 through .7), preserved across owner restarts. Inspect the last
begin without a matching completion when diagnosing a blocked graphics call.
`diagnostic_error` in the status snapshot exposes journal I/O failures. Logging
does not reset capture, change source selection or prove a caller caused a
graphics-kernel deadlock. A sudden power loss may still lose an incomplete write.

Build against the matching OpenRGB API-5 headers and Qt 6.8.3 with
`scripts/Build-Office.ps1`. The Windows workflow pins the native source revision.
The production D3D11 shader is exercised using WARP by `office_capture_tests`;
actual HWND capture and controller output still require the real office setup.

## Native control panel

Use the delivered `office/Open-Office-Control.ps1` launcher. Control offers
enable, output FPS, smoothing, spatial calibration and return to ambience.
Control now groups sampling algorithms, fixed/adaptive depth, black-bar exclusion,
spatial blur, saturation/brightness/luminance floor, time/EMA smoothing, scene-cut
thresholds, presets and manual standby-effect previews. Lock/display sleep always
rests the lights regardless of that preview selection. These settings execute in
the native sampler/output; they are not preview-only filters.
Black-bar detection accepts centered paired mattes only when their boundaries fit
4:3, 16:10, 16:9 or common cinema ratios (1.85–2.76), the outer/middle/inner matte
probes are consistently near black, and adjacent content is positively visible.
Ratio alone, a dark scene, a one-sided dark edge or an entirely black frame cannot
cause cropping. One GPU pass computes the rectangle shared by all LEDs; normalized
regions scale into that content rectangle without changing saved topology. The
full capture remains visible in the editor with effective sampling outlines.
Topology shows the actual downscaled main-display capture, per-LED sampling rectangles,
hardware LED names/indices, and current output colours. Select a device/zone or
one LED; drag, resize, quarter-turn or reverse its sampling order. Click a device
row to select all its zones, Ctrl-click to add/remove entries, or Shift-click a
range. Drag any selected area or use arrow keys to move the selection together;
spacing and physical LED identities are preserved. One undo restores the entire
gesture and Escape cancels it. The Show choice defaults to Selected devices;
Selected zones hides unselected zones and All devices gives a complete overview.
Lighting enable checkboxes remain independent of editor selection. Coordinates
are percentages of the main-display image, with Y increasing downwards. Devices includes
device/zone enable and individual/all-device two-second identify. Eight constant
pixel corner/edge handles resize regions; grid/snap, pan/zoom, undo/redo and edge
presets retain hardware LED identity. Cyan shows the editable region; dashed amber
shows the actual GPU sampling rectangle, including black-bar/depth adjustment.
The blur footprint is shown separately. Status exposes capture errors,
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
The Preview and Topology source images preview the unsaved blur slider through
the same GPU filter as LED extraction. This read-only override changes neither
the saved map nor the actual lighting until Apply. The LED overlay continues
showing submitted colours. Preview caching includes source revision and draft
blur, and the panel rejects images for an older slider value. Returning to zero
uses the original retained input; it does not blur an already-filtered bitmap.
The Preview page shows actual capture and submitted LED colours. Its separate
MIT-licensed animation/music playground retains the original six WebGL effects,
particles, palettes, postprocessing, audio bindings, visualizers and saved presets.
It is local browser content, not another RGB writer: maximize it on the main display to use
the existing app-capture policy. Controls fade after four seconds; moving inside
the page shows them again, and H toggles manual hiding.
Settings tests/reconnects the same-user native pipe, opens diagnostics, and invokes
the packaged verified stop/restart scripts. Its startup preference means starting
lighting when opening this panel, not adding a Windows boot task. The panel can
remain in the tray with unsaved draft edits. All six office controllers, including
keyboards, are editable; disconnected devices retain their maps.

This fork is under active integration. A successful build alone is not physical
acceptance or final delivery. The core integration specification owns those gates.

# Corresponding-page visual audit — 2026-11-04

**Result: visual parity fails.** The native panel preserves much of the working
behavior, but its appearance is a generic Qt form with a related palette. The
earlier functional reconciliation did not establish the requested visual match.
No native UI code, device settings or hardware maps were changed for this audit.

Unchecked items below are differences to resolve and verify, not completed work.
Source-only observations are labelled; a screenshot cannot establish animations,
hidden controls or the appearance of a populated list that it does not show.

## Evidence and exact page correspondence

Native baseline: Ambient `4319799c318e52acf1f1704f25265c0d8449777f`, local
unpublished package `1.1-office.0.4.0`. Actual native exports are in
`D:/Tools/OpenRGB/validation/20261104/gui-0.4.0/versioned-preflight-views`.
The same six pages exist at 1000×700, 1440×900 and 1920×1080. Comparisons below
use 1440×900 exports against the modern 1440×860 reference family, comparing
content geometry rather than pretending the two viewport heights are equal.

| Native page | Corresponding repository screenshot | Other inspected gallery evidence |
| --- | --- | --- |
| Control: `1440x900-page0.png` | [ed-dashboard.png](ed-dashboard.png), upstream `docs/ss-dashboard.png` | [screenshot-index.png](screenshot-index.png): older large centered title, different dashboard grid |
| Topology: `1440x900-page1.png` | [ed-topology.png](ed-topology.png), upstream `docs/ss-topology.png` | Recovered `web/topology-editor.html` and `web/js/topology-editor.js` for selected/hidden states |
| Devices: `1440x900-page2.png` | [ed-devices.png](ed-devices.png), upstream `docs/ss-devices.png` | [screenshot-devices.png](screenshot-devices.png): older shell/card style |
| Settings: `1440x900-page3.png` | [ss-settings.png](ss-settings.png), the same Settings design supplied by the user | [screenshot-settings.png](screenshot-settings.png): older centered heading and single-outline cards |
| Native live Preview: `1440x900-page4.png` | **No corresponding native-capture screenshot** | [ss-preview.png](ss-preview.png) shows the procedural playground; compare with our separately imported playground, not the native capture image |
| Startup / connection failure | [ss-loading.png](ss-loading.png) | [screenshot-loading.png](screenshot-loading.png): older startup design and an actual original error |
| Native Status: `1440x900-page5.png` | **No gallery counterpart** | Recovered `help.html` is a separate Help page, not a raw Status page |

All ten gallery PNGs above were viewed in this audit. Modern `ss-*` designs are
the primary target, following the user's supplied Settings reference. Older
`screenshot-*` variants are historical evidence, not interchangeable targets.
Connected devices, actual wallpaper, saved values and active/inactive states
differ between captures; those data differences are not visual defects.

Original release: EDAmbilight v1.2.0, archive SHA-256
`9b73dc1b13917d4566a960767d2000894ba23adb4c29e7b8e5bef5d0708c2a7c`.
Authenticated recovered HTML/CSS/JS is retained at
`D:/Tools/OpenRGB/validation/20261104/edambilight-gui-v1.2.0`.
The release includes later controls not visible in the repository screenshots;
those must be styled consistently rather than deleting them to match an older image.

## A. Shared shell and navigation

- [ ] **A01 — Rail width:** reference is exactly 80px; native source fixes 88px and adds surrounding layout margins. Match the reference rail and content origin.
- [ ] **A02 — Brand mark:** reference uses the original brightness glyph inside concentric rings; native shows plain `RGB` text. Reuse the recovered glyph and ring construction.
- [ ] **A03 — Logo surround:** reference has an inset circular core, cyan outline, radial illumination and expanding outer rings; native has no corresponding treatment.
- [ ] **A04 — Navigation symbols:** reference has brightness, hub, chip, tune and sparkle symbols; native has none. Use the exact Material Symbols glyphs listed below.
- [ ] **A05 — Navigation label geometry:** reference places a small uppercase label below its icon; native centers mixed-case text inside a large button. Match stacking, spacing and label treatment.
- [ ] **A06 — Active marker:** reference has a narrow glowing left bar and subtle horizontal gradient; native uses a bright outline around the whole rounded button.
- [ ] **A07 — Active versus focus state:** native exports show an extra cyan outline on Control while another page is selected. This is visible focus styling, not proof that both pages are active; make focus distinct from the active marker.
- [ ] **A08 — Rail separators and version:** reference has top/bottom hairlines and a bottom version label; native has neither version placement nor the same separators.
- [ ] **A09 — Page header:** reference names the current page (`AMBILIGHT`, `TOPOLOGY EDITOR`, `DEVICES`, `SETTINGS`); native repeats `Office Ambilight` on every page.
- [ ] **A10 — Header composition:** reference has short cyan rules, a tracked subtitle and a bottom hairline in a compact approximately 46px header; native uses a large title plus connection sentence and consumes more vertical space.
- [ ] **A11 — Page destinations:** recovered navigation has Help; native substitutes raw Status. Keep useful diagnostics, but supply the missing help/onboarding surface and clear navigation rather than claiming they are equivalent.
- [ ] **A12 — Persistent footer:** native shows `Saved`, `Reload saved map` and `Save changes` on every page; reference uses page-specific actions. Restrict mapping actions to the relevant editor and avoid implying that an About/Preview page has an unsaved map action.
- [ ] **A13 — Background:** reference has subtle radial illumination, grain and faint scan/hairline structure; native is largely a flat dark fill. Match restrained depth without making animation a capture workload.

## B. Typography, surfaces and common controls

- [ ] **B01 — Display font:** original headers and numerical readouts use shipped Orbitron; native explicitly loads Segoe UI. The actual Orbitron files are available.
- [ ] **B02 — Body font:** original uses shipped Rajdhani; native uses Segoe UI. Match family, weight and density while retaining legible text.
- [ ] **B03 — Type hierarchy:** reference separates tracked uppercase card eyebrows, readable labels, subdued helper text and prominent cyan values; native group titles and ordinary labels dominate uniformly.
- [ ] **B04 — Card construction:** reference has a rounded outer bezel plus inset inner border; native has a single QGroupBox outline interrupted by its title.
- [ ] **B05 — Card depth:** reference uses translucent gradients, inset highlights and soft shadows; native has flat blue panels with no equivalent depth.
- [ ] **B06 — Radius and accent details:** modern reference uses roughly 20px outer card rounding and delicate cyan corner accents; native uses 12px groups and different border thickness/colour. Do not mix the older square-corner screenshots with this target.
- [ ] **B07 — Label backgrounds:** native renders dark rectangular strips behind several labels, checkbox rows and text blocks; reference labels sit directly on the card surface.
- [ ] **B08 — Sliders:** reference shows fine tracks, cyan progress and circular luminous handles; actual native exports show small dark square handles and blue lines, despite attempted stylesheet rules. Verify the rendered control, not the stylesheet text.
- [ ] **B09 — Values and steppers:** reference emphasizes a cyan number above/beside the slider; native makes boxed spin controls and two vertically stacked arrows the dominant element. Use compact editable values and matching arrow geometry.
- [ ] **B10 — Toggles:** reference power/enable controls are glass pill switches with clear on/off thumb states; native uses ticks/checkbox indicators, some unchecked indicators almost disappearing into the background.
- [ ] **B11 — Select fields:** reference has dark recessed fields with compact cyan chevrons; native uses lighter filled controls with a separate painted arrow block. Match dimensions, fill and chevron placement.
- [ ] **B12 — Buttons:** reference uses compact icon-plus-label actions, distinct primary/secondary treatments and restrained glows; native uses wide, equally prominent text-only rectangles.
- [ ] **B13 — Icons in card headers/actions:** reference has small cyan icon badges and source-specific symbols; native omits them throughout Settings, Devices and Topology.
- [ ] **B14 — Scrollbars:** native exports show conspicuous platform arrow buttons and outlined scroll tracks; reference uses much quieter scrollbar styling.
- [ ] **B15 — Motion and interaction states (source evidence):** recovered CSS defines hover, press, reveal, active glow and sonar animations; native source has only limited border/hover styling. Reproduce useful state feedback, honour reduced motion, and verify runtime behavior separately from static screenshots.
- [ ] **B16 — Spacing and alignment:** reference card padding, label baselines, action spacing and small helper rows form a consistent hierarchy; native form-layout stretching introduces large unrelated gaps and misaligned readout/slider rows.

## C. Control compared with Control

- [ ] **C01 — Dashboard composition:** modern reference places a tall Status card at left, three top cards and four lower cards to its right, then a wide Devices area; native uses a uniform three-column grid across three rows.
- [ ] **C02 — Status span:** reference Status spans the upper rows, with a separated bottom enable switch; native confines it to one shallow top-left card.
- [ ] **C03 — Status presentation:** reference has a state dot, prominent state word, short helper line and central indicator; native shows a checkbox above a dense multiline capture/latency paragraph.
- [ ] **C04 — FPS emphasis:** reference shows a large cyan `15`, small `fps` unit and slider; native starts with a full-width boxed `15 FPS` spinner and a small slider.
- [ ] **C05 — Capture hierarchy:** reference capture choice and active-engine readout occupy a concise top-row card; native uses a static multiline policy explanation. Preserve truthful WGC-only policy, but use the reference hierarchy for engine/source/status.
- [ ] **C06 — Algorithm location:** reference puts Algorithm at top right with black-bar toggle below; native puts it at row two left, separated by a large vertical gap from its checkbox.
- [ ] **C07 — Fixed/adaptive grouping:** reference separates Sampling Area and Adaptive Area into adjacent cards; native combines all three depths in a single generic form.
- [ ] **C08 — Strategy-dependent visibility (source evidence):** original dims/disables controls irrelevant to the selected strategy; native visibly presents all depth and temporal fields together. Reconcile applicable field states without silently changing sampler behavior.
- [ ] **C09 — Temporal layout:** reference shows right-aligned cyan EMA and scene-cut values with sliders/helper lines; native stacks a mode combo, adaptation spinner, EMA row and scene-cut row inside a tall form.
- [ ] **C10 — Output layout:** reference gives saturation a clear readout/slider hierarchy; native gives saturation, brightness and luminance equal boxed rows. Keep the additional working controls, grouped in an appropriate expanded area.
- [ ] **C11 — Primary viewport density:** reference core controls and Devices area fit the supplied modern screenshot; native already scrolls before the bottom Output controls at 1440×900.
- [ ] **C12 — Devices on dashboard:** reference reserves a large lower Devices card and explanatory topology note; native has a summary label below the grid and moves interactive device cards exclusively to another page.
- [ ] **C13 — Device card treatment (source evidence):** original dashboard device cards have type icon, LED/type/zone metadata, power button and expandable zones; native dashboard lacks that card composition.
- [ ] **C14 — Presets/advanced controls (release source beyond screenshot):** original uses styled preset chips and disclosure; native has plain buttons and an advanced checkbox below the fold. Preserve functionality with the recovered presentation.
- [ ] **C15 — Additional blur/standby/test controls:** native places these as primary grid cards or large buttons; they alter the reference's composition. Keep the requested working features but place them coherently in expanded sections.

Do not copy reference sample values over saved user settings. Nor should the
reference's historical “keyboards excluded” note remove our two required keyboards.

## D. Topology compared with Topology

- [ ] **D01 — Column composition:** reference uses separate left Device, Grid Config and Zones cards; native uses one tall Qt tree and moves Grid/Snap controls elsewhere.
- [ ] **D02 — Grid controls:** reference has a left Grid Size readout/slider plus Snap/Show checkboxes; native splits toggles into the top toolbar and a boxed grid percentage into the right inspector.
- [ ] **D03 — Main frame:** reference wraps the canvas and bottom toolbar in a double-bezel card; native canvas floats in a flat central area between splitter handles.
- [ ] **D04 — Canvas backdrop:** reference shows a dark, restrained grid; native overlays a very dense bright grid on the image. Preserve the real capture preview, but separate useful sampling overlays from background grid noise.
- [ ] **D05 — Bottom toolbar:** reference groups Reload, Auto Layout, Reset All, Fit View, Calibration and Save Topology below the canvas; native spreads Undo/Redo/Fit above it, reset/identify into the inspector and Save into a global footer.
- [ ] **D06 — Toolbar icons:** reference supplies refresh, grid_view, restart_alt, fit_screen, adjust and save symbols; native uses text only.
- [ ] **D07 — Auto-layout versus reset:** reference exposes distinct actions; native labels one action `Auto layout / reset map`. This is an interaction/semantic gap as well as a layout difference, requiring reconciliation rather than another cosmetic label.
- [ ] **D08 — Inspector framing:** reference has a compact Zone Inspector card and a separate Effect Center card visible at once; native has a much taller Sampling Area form that pushes Effect Center below the viewport.
- [ ] **D09 — Effect Center geometry:** reference places Center X/Y side by side and offers small preset chips; native stacks large spin boxes and uses a preset combo.
- [ ] **D10 — Inspector action density:** native Rotate, Reverse, edge mapping and Identify occupy full-width tall controls; reference uses compact rows with iconography and smaller actions.
- [ ] **D11 — Tree/zone presentation:** reference differentiates device and zone panels with card eyebrows and subdued metadata; native uses ordinary tree indentation, repeated checkmarks and frequently truncated device names.
- [ ] **D12 — Selection and direction:** native draws a large diagonal direction arrow across selected matrix geometry. The reference's strip/zone cues are more restrained; preserve hardware order but make direction readable without obscuring the preview.
- [ ] **D13 — Status strip:** reference has a dedicated bottom information strip for controls, coordinates, zoom and bounding box; native only shows two prose help lines without that structured presentation.
- [ ] **D14 — Physical-length/spacing controls (release source):** original inspector exposes physical length and LED spacing; native normalized sample rectangles have no equivalent explicit fields. Existing normalized geometry is an adaptation, not proven parity for those settings. Record the equivalent behavior before marking this requirement satisfied.
- [ ] **D15 — Calibration disclosure (release source):** original places advanced calibration in its own disclosed area; native distributes calibration between Control and inspector actions. Reconcile discoverability and clear return-to-ambience feedback.
- [ ] **D16 — Empty selection/device states:** reference uses explanatory card content and `device_unknown` icon with a Device Management link; native empty-tree/inspector states have no matching illustrated treatment.

The live 21:9 capture image, individual LED editing, matrix geometry, eight resize
handles, pan/zoom and correct hardware identities are requirements to preserve.
Their absence in the reference's empty screenshot is not a reason to remove them.

## E. Devices compared with Devices

- [ ] **E01 — Summary cards:** reference has three horizontal cards for Total Devices, With Topology and Actions; native replaces them with one text line and an Identify All button.
- [ ] **E02 — Large numeric metrics:** original totals use cyan/neutral Orbitron numbers; native counts are ordinary body text.
- [ ] **E03 — Actions block:** reference gives Flash All an icon, compact action card and helper text; native places a lone button at the far edge of the page.
- [ ] **E04 — Device card structure (release source):** reference populated cards have type icon badge, name, type/LED/topology metadata and compact actions in one header row; native uses vendor-named QGroupBoxes with device and zone checkboxes in repeated rows.
- [ ] **E05 — Type badges:** reference distinguishes matrix, linear and single devices with badge/icon colour; native lacks those type badges and symbols.
- [ ] **E06 — Enable/disabled appearance:** reference uses pill switches and dims disabled cards; native uses ticks without the same card-state hierarchy.
- [ ] **E07 — Per-zone actions:** native repeats a large Edit Topology button on each zone row; reference uses more compact topology links and device-level actions. Retain per-zone access without duplicating large visual weight.
- [ ] **E08 — Topology state:** reference distinguishes mapped zone counts from No Topology with explicit action text; native focuses on connectivity/LED counts. Both states need clear, separately labelled representation.
- [ ] **E09 — Empty/reconnect state:** repository shows a centered `device_hub` symbol, explanatory text and (in shipped source) Rescan; native should provide an equally clear state while preserving offline maps. A live connected screenshot cannot validate this state.

The gallery's zero devices and our six devices are different runtime states, not
a mismatch to fix by hiding devices. Populated reference rows above are source
evidence; no claim is made that the gallery demonstrates them.

## F. Settings compared with the supplied Settings screenshot

- [ ] **F01 — Content width:** reference uses `max-width:640px; margin:0 auto`; native spans approximately 1300px at the same desktop width.
- [ ] **F02 — Vertical origin:** reference starts its centered card column just below the compact header; native uses the global title/connection block and full-width groups.
- [ ] **F03 — Card heading composition:** reference pairs a 32×32 cyan icon badge with uppercase eyebrow and subtitle; native uses a QGroupBox caption cutting through its border.
- [ ] **F04 — Connection field composition:** reference has Host and Port side by side (`1fr 140px`, 12px gap); native has one full-width read-only state-directory field. The native transport is intentional, but its truthful connection fields should follow the same compact visual hierarchy.
- [ ] **F05 — Connection status row:** reference places a status dot/text at lower left and compact Test/Save & Reconnect at lower right, separated from fields by a hairline; native has transport prose followed by two equally wide buttons.
- [ ] **F06 — Connection action icons:** reference uses `wifi_tethering` and `save`; native has neither symbol.
- [ ] **F07 — Section substitution:** screenshot has Connection, Web Server and About; native has Connection, Application and About. A web-port control is inapplicable to our pipe-only native owner, but the replacement card must retain the same visual construction and rhythm. Do not add a fake web server to resemble the screenshot.
- [ ] **F08 — Application controls (later shipped release):** recovered v1.2.0 legitimately adds executable/autostart and Application actions beyond the screenshot. Native checkbox strips/full-width buttons still do not match its compact icon-badged card and small action row.
- [ ] **F09 — About wordmark:** reference has a prominent cyan Orbitron name; native begins with a small plain-text version sentence.
- [ ] **F10 — About version:** reference aligns a subdued version beside the wordmark; native places version in the same sentence with the name.
- [ ] **F11 — About divider/body:** reference separates branding from a short description with a hairline and balanced padding; native puts all text in one dark-backed paragraph.
- [ ] **F12 — Local action scope:** reference Settings has no persistent map-save footer; native displays Reload Saved Map/Save Changes even here.
- [ ] **F13 — Whitespace:** reference centers its compact cards with intentional surrounding space; native leaves most of the lower screen empty after wide shallow forms. Matching background colour does not establish matching composition.

Reference-specific source anchors: recovered `web/settings.html` header and
`max-width:640px` wrapper; `.section-icon` 32px/8px radius; panel body 22px×24px;
`.about-name` Orbitron 1.5rem/700; `.about-ver` Orbitron .72rem/700.

## G. Preview, loading and help

- [ ] **G01 — Preview destination:** original Preview enters the full-window procedural scene; native Preview opens a captured-wallpaper image with overlays and a secondary playground button. These are two different tools and should be labelled/routed explicitly.
- [ ] **G02 — Playground correspondence:** the imported original playground is the correct surface to compare with `ss-preview.png`. Its reused code/assets do not establish visual parity for the five other native pages.
- [ ] **G03 — Integrated presentation:** the playground opens separately through the default browser, while the native shell keeps its own rail/footer. Original preview is a full-window scene with an overlaid translucent control panel and Back action. Record and resolve this navigation/presentation difference.
- [ ] **G04 — Live preview controls:** native live preview uses an ordinary checkbox and wide text button; give the additional tool the same icon/control language instead of treating it as the original Preview screenshot.
- [ ] **G05 — Startup composition:** original has a centered circular SVG emblem, rings, uppercase wordmark, subtitle, progress line and loading state; native opens its ordinary panel with a connection sentence. There is no matching startup presentation.
- [ ] **G06 — Connection failure:** modern original shows a bounded amber warning card with warning icon, explanation and Retry/Continue actions; native uses an amber connection sentence and diagnostics. Match the error hierarchy with actions valid for our native owner.
- [ ] **G07 — Original error text:** older loading screenshot contains an Eel duplicate-exposure error. That is original runtime content, not a visual feature or a bug to reproduce in our application.
- [ ] **G08 — Help versus Status:** native raw JSON plus Recover Capture is useful diagnostics, but does not implement the recovered Help/onboarding page. There is no corresponding Help gallery screenshot, so use the shipped source to define it and keep diagnostics separately accessible.

## Actual recovered iconography

**Recovery complete; native adoption incomplete.** The symbols are available,
not something that needs to be guessed from screenshots.

- [x] Verified all 12 already-vendored font files byte-for-byte against the retained release manifest: Material Symbols Outlined; Orbitron weights 400–900; Rajdhani weights 300–700.
- [x] Catalogued 34 distinct used glyphs and 64 literal/resolved references, retaining page, line, exact style and dynamic-selector evidence in [iconography/manifest.json](iconography/manifest.json).
- [x] Exported the original [power SVG](iconography/ambilight-1.svg) and [loading emblem SVG](iconography/loading-1.svg); only an absent XML namespace was added.
- [x] Rendered the [icon contact sheet](iconography/contact-sheet.png) using the actual local font: outline/weight 200 and active filled/weight 300. Browser font readiness confirms all three families loaded; glyph width is 32px rather than fallback ligature text.
- [ ] Wire these assets into the native UI and verify actual native rendering; a successful browser catalogue is not a Qt rendering result.
- [ ] Preserve original code attribution and confirm individual font license notices before additional redistribution. The release's MIT code license is not assumed to replace font licensing.

| Location | Exact shipped glyph / construction |
| --- | --- |
| Rail logo, Control | `settings_brightness`, logo surrounded by CSS rings |
| Topology | `hub` |
| Devices | `memory` |
| Settings | `tune` |
| Preview | `auto_awesome` |
| Help | `help` |
| Settings section badges | `hub`, `http`, `power_settings_new`, `info` |
| Connection test / save | `wifi_tethering`, `save` |
| Device/zone actions | `bolt`, `hub`, `refresh`, original power SVG |
| Device types | `keyboard`, `linear_scale`, `fiber_manual_record` |
| Empty states | `device_hub`, `device_unknown` |
| Topology actions | `restart_alt`, `refresh`, `grid_view`, `fit_screen`, `tune`, `adjust`, `save`, `swap_horiz`, `center_focus_strong` |
| Preview controls | `arrow_back`, `fullscreen`, `music_note`, `play_arrow`, `volume_up`, `delete` |
| Notices | `check_circle`, `error`, `warning`, `info` |
| Startup | Original inline SVG emblem plus CSS rings/glow/progress |

Navigation source metrics: 26px glyph; inactive `FILL=0,wght=200`, active
`FILL=1,wght=300`; `GRAD=0,opsz=24`. Logo glyph is 18px, weight 200, optical size
20. Labels use Rajdhani 11px/700, uppercase, tracking .07em, with 7px icon gap.
The active cyan bar is 2.5px wide with 8px top/bottom inset. These are recovered
source values, not inferred measurements.

Material Symbols file SHA-256:
`841ce0fe3a3d05e8a63b0f5ed60cc1329d1e4f9b50975e4063699b16981b7705`.
Existing asset: `third_party/ed-preview/fonts/MaterialSymbolsOutlined.woff2`.
[Interactive catalogue](iconography/contact-sheet.html) uses those existing bytes;
no substitute icon pack or new font dependency was introduced.

## Acceptance and next implementation boundary

This checklist completes the requested comparison and icon catalogue. It does
**not** close visual implementation. Stage 8 remains ACTIVE; stage 9 publication
is BLOCKED by that visual acceptance. Functional sampler/hotplug evidence remains
valid for its tested scope; local 0.4.0 is unpublished and visually unaccepted.

Next implementation must first fix the shared shell/type/icons/control system,
then reproduce the corresponding page composition. Verify actual native renders
against each named modern reference at representative sizes; separately exercise
populated/empty/offline/selected states absent from gallery images. Preserve the
single native owner, live capture, hardware LED identities and all saved maps.

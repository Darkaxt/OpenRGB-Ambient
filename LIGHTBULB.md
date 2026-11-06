# Integrated screen adjustment

Upstream: https://github.com/Tyrrrz/LightBulb
Incorporated reference: f74285784be7bc191872c06b1b5e644eb209b98b (2026-11-06 inspection).
License: LICENSE-LightBulb-MIT.txt; the unmodified upstream favicon.ico is embedded.
Authoritative requirements and stage status: native OpenRGB repository's
office-integration-specification.md, R29-R34.

| Upstream source | Native adaptation |
| --- | --- |
| LightBulb.Core/Cycle.cs, SolarTimes.cs | OfficeScreenPolicy.h, source-derived cycle/solar regression vectors |
| Services/GammaService.cs, PlatformInterop/DeviceContext.cs | OfficeScreen.h, native gamma ownership, lifecycle/readback/restoration |
| Services/SettingsService.cs, ViewModels/Components/DashboardViewModel.cs | OfficeScreen.h, private settings import, offsets, exclusions, hotkeys, cycle preview |
| App.axaml, Views/Components/Settings/* | OfficeScreenPanel.inc, Screen page and exact two root pause submenus |
| favicon.ico | assets/office/lightbulb.ico |

This is a native Qt/C++ adaptation, not a second .NET/Avalonia runtime. Preserve
source provenance and compare all mapped upstream files during monthly maintenance.
Keep application startup/update/preferences owned by Office. Private LightBulb
configuration is imported into office-screen.json; never package that file or
location/exclusion/hotkey data. RGB topology remains office-ambience.json.

Pause deadlines represent explicit user choices, not operation-cancellation timers.
Gamma baseline restoration is independent of capture teardown. The scheduler runs
on the existing worker; GUI work must not stall display or RGB ownership.

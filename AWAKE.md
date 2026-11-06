# Main-display application awake policy

Inspected Microsoft PowerToys Awake at commit
ba946c9885d189dda9b40e2990c5cbf3b4835bbc (2026-11-06):
`src/modules/awake/Awake/Core/Manager.cs`, `Core/Native/Bridge.cs` and the module
README. Awake serializes `SetThreadExecutionState` calls and clears its execution
state on release/session changes. No PowerToys executable or runtime is embedded.

Office uses a handle-scoped display/system power request on its existing worker.
This API choice satisfies the actual screensaver requirement: Microsoft's
SetThreadExecutionState documentation says it does not stop a screen saver;
POWER_REQUEST_TYPE documents that PowerRequestDisplayRequired prevents automatic
screensaver activation. PowerRequestSystemRequired accompanies it to keep the
system running while the display is required.

- https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate
- https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-powersetrequest
- https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/ne-wdm-_power_request_type

The real main-display app selector owns eligibility. No browser playback signal,
synthetic input, screen-saver configuration change or permanent power-plan edit.
One request is held while a qualifying app remains; restore/minimize/removal or
moving it off the main display releases it. Lock, suspend, secure desktop, display
off and shutdown release it too. Normal desktop idle/screensaver rest and manually
launched savers retain their existing behavior. Diagnostic state lives in the
existing owner snapshot; the request reason contains no private window title.

The tray's Awake submenu also adapts TrayHelper.cs and GetDefaultTrayOptions:
Automatic, interval (30 minutes/1 hour/2 hours or imported custom choices), local
date/time expiration, indefinitely, Off and Keep screen on. Automatic is the
initial mode and always requires the display; manual modes explicitly override it.
Off releases every Office request. Manual display-off preference holds only the
system request, including while the monitor is off. Display-off releases
Automatic and manual display-on requests. Explicit timed/expiration deadlines return to Off and survive
restart in office-awake.json. Private preferences are imported without changing
PowerToys' files; its other utilities and runner remain independent.

Automatic screensaver rest holds both RGB output and Awake released until a new
Windows session input-event timestamp is observed on a usable normal desktop.
Returning to Default, display/topology changes and fresh wallpaper frames alone
cannot resume output. The gate survives process restart in private
office-rest.json. GetLastInputInfo supplies a session input signal (including
injected input), not physical-user attestation; only timestamps are retained.
Reference: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getlastinputinfo

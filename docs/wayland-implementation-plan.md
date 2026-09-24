# PlasmaGlow: KDE Wayland implementation plan

Date: 2026-09-24  
Status: core Wayland behavior is user-verified in a live session. Live X11
verification remains pending; per-account persistence has been checked at the
configuration-file level for both local users.

## 1. Goal and agreed scope

Keep the applet's familiar saturation and gamma controls and add KDE Plasma Wayland
support through a PlasmaGlow-owned KWin effect written in C++.

- The utility is primarily for personal use with one monitor.
- On Wayland, correction is global across windows and outputs.
- Selecting individual outputs on Wayland is outside the current scope.
- Settings are per Linux account, shared by that account's X11 and Wayland
  sessions, and stored outside the system-wide applet/effect installation.
- Each active desktop session has its own KWin effect instance. The applet applies
  that account's saved values when the effect becomes ready.
- Per-application profiles, focus tracking, and game modes are not needed.
- kgamma2 and Satur8 are references for behavior and architecture. Their source is
  not copied or ported, and the applet will not depend on either application or its
  plugins.
- Use the platform's normal Qt, Plasma, and KWin APIs.
- The target Wayland design is one PlasmaGlow-owned effect with saturation and
  gamma controls exposed to the applet over D-Bus.
- A separate CLI tool or daemon is not currently needed.
- X11 continues to use the existing `vibrant-cli`, `xgamma`, and `xrandr` tools.
- The user makes commits.

## 2. Existing behavior

`src/glowcontroller.cpp` currently combines settings, state queries, and external
process calls. QML reads and writes the controller's properties directly.

| Function | Current X11 behavior | Target Wayland behavior |
| --- | --- | --- |
| Saturation | `vibrant-cli` for the selected output | PlasmaGlow effect, all windows and outputs |
| Saturation range | 0.0–4.0, UI step 0.05 | Same range and step |
| Gamma | `xgamma`, independent of the selected output | PlasmaGlow effect, all windows and outputs |
| Gamma range | 0.1–5.0, UI step 0.05 | Same range and step |
| Neutral value | 1.0 for both controls | 1.0 for both controls |
| Presets | Saturation 1/1.5/2/3; gamma 0.8/1/1.2/1.5 | Same presets |
| Persistence | `General` group in `plasmaglowrc` | Read and write compatible settings |
| Output selector | Affects saturation | Applies globally; no selector |
| Refresh | Queries outputs and current values | Checks the effect connection and reads its state |

Gamma on X11 is not tied to the output selected in the ComboBox. Settings contain
one saturation/gamma pair, not independent profiles for each monitor.

## 3. Architecture

```text
QML: sliders, presets, reset, availability state
                      |
                 GlowController
          settings + application state
                  /          \
           X11 backend     KWin backend
         external tools      Qt D-Bus
                                |
                    PlasmaGlow KWin effect
                    saturation + gamma, C++/GLSL
```

`GlowController` remains the QML entry point and owner of user settings. Two small,
concrete backends separate system operations from UI and configuration. A general
backend framework is unnecessary.

The effect is a separate CMake target loaded into KWin. The applet talks to it over
the session bus. The effect does not read or write `plasmaglowrc`; the applet
controller remains the single owner of saved values.

Keep desired values from settings distinct from values confirmed by the effect.
A failed request must not be shown as successfully applied.

## 4. Phase 1 — rendering point and color math

**Goal:** confirm a suitable way to implement the effect against the installed KWin
API before connecting the UI.

1. Review the installed KWin headers and official effect examples for rendering,
   texture color spaces, alpha, and bypass behavior.
2. Choose between processing the complete output and processing every window. Record
   the choice and its consequences here.
3. Account for the fact that nonlinear gamma before transparent-window blending
   differs from gamma applied to the completed output image.
4. Define the working color space, order of saturation and gamma, alpha handling,
   treatment of negative values, and range limits.
5. Implement the math independently. For saturation, scale the RGB deviation from
   the selected luminance. For gamma, begin with the curve `x^(1/gamma)`. Document
   the coefficients and operation order.
6. Do not claim that the shader curve is equivalent to `xgamma` or an ICC VCGT.
   Equal numbers should have a clear meaning; visual similarity requires separate
   confirmation.

**Research result (2026-09-24):** the installed development headers are from KWin
6.7.5. KWin exposes `OffscreenEffect`, which redirects a window to a texture and
lets an effect shader transform that texture. KWin's built-in Invert and Color
Blindness Correction effects use this API on existing and newly added windows.
This supports a global all-window effect across outputs without application
tracking. The shader runs on each window before final composition, so transparent
surfaces can differ from a transform applied once to the completed output. This
is the accepted first implementation route; a final-output LUT or ICC solution is
not being added as an automatic fallback.

Use the scene's SDR linear RGB for the initial math. Unpremultiply RGB before the
color transform, preserve the original alpha, then premultiply RGB again so the
transform does not change a surface's transparency. Apply saturation around the
Rec.709 luminance component, then apply `max(rgb, 0)^(1/gamma)` per channel. KWin's
color-management path converts the window texture into the scene blending space;
HDR and wide-gamut behavior still require separate investigation and acceptance.

Relevant API evidence: `/usr/include/kwin/effect/offscreeneffect.h`,
`/usr/include/kwin/effect/effect.h`, and the installed KWin package version. The
official implementation examples are KWin's `invert` and
`colorblindnesscorrection` effects in the [KWin source tree](https://invent.kde.org/plasma/kwin).

**Check:** small numerical checks for neutral values, gray at zero saturation,
gamma direction, and finite results. Use a few fixed colors with expected results
to check alpha and operation order.

**Complete when:** the rendering method and math are selected and their limitations
are clear. If gamma at this point does not provide the intended behavior, record
that before integration. An approach using KScreen/ICC requires a separate
decision; do not silently add it as a fallback.

**Status:** source/API review is complete. In a live Wayland session, the user
confirmed gamma adjustment, saturation at zero producing grayscale, and presets.
Transparency and HDR/wide-gamut behavior remain unconfirmed.

## 5. Phase 2 — PlasmaGlow KWin effect

**Goal:** create a standalone effect with saturation and gamma parameters.

1. Add the effect directory, C++ class, shader, and PlasmaGlow-owned metadata.
2. Add a separate CMake target using the installed KWin development package. Do
   not port build workarounds from other projects.
3. Add an explicit build option. The X11 applet must remain buildable without KWin
   headers; when the effect is requested, configuration must report missing
   dependencies clearly.
4. Use a PlasmaGlow-owned plugin ID and D-Bus endpoint.
5. Define a small API contract: API version, state readback, and one atomic
   `saturation, gamma` apply request.
6. Validate finite numbers and ranges in the effect. A rejected request must leave
   the currently applied pair unchanged.
7. Request a repaint after parameter changes, including for a static desktop. Cover
   windows created after the effect is enabled.
8. At `(1, 1)`, bypass the extra shader work. When using window redirection,
   unredirect windows while neutral.
9. Report shader creation failures. A D-Bus reply must not report success when the
   shader is unusable.

**Check:** build against the installed KWin package; check the D-Bus contract,
invalid values, repeat application of the same pair, and reset. After installing
the plugin, visually inspect a color chart, transparent windows, and new windows.

**Complete when:** the effect works independently from the applet, resets cleanly,
and has confirmed visual behavior in a tested session. A KWin X11 check, if
available, does not replace Wayland acceptance.

**Implementation progress (2026-09-24):** the separate `plasmaglow` CMake target,
KWin plugin factory, D-Bus state/apply API, and GLSL adjustment shader are in
place. The target is optional and uses C++20 plus the installed KWin, Qt DBus,
and Qt Widgets development targets. Configure and module builds succeeded against
KWin 6.7.5. The effect registers a PlasmaGlow-owned session-bus service so the
applet can observe effect unload and reload. It is installed system-wide and has
been exercised in Wayland: D-Bus reports API v1 and ready, while the user confirms
gamma, grayscale at saturation zero, and presets work. Updating a loaded effect
requires a new Wayland session; an in-process unload/load kept the old plugin code.
Invalid requests, transparent windows, and newly opened windows still need checks.

## 6. Phase 3 — separate the controller and preserve X11

**Goal:** prepare the current applet for two backends.

1. Keep settings and public UI properties in `GlowController`; move X11 system
   operations into a concrete class.
2. Select a backend from the actual session and required interface availability.
   `DISPLAY` under XWayland does not mean the session is X11.
3. Represent saturation and gamma availability independently, along with
   connection state, application state, and the latest error.
4. Remove blocking process waits from the UI thread. State queries and apply
   requests need a completion result and bounded timeout.
5. Check external command results. A failed query must not overwrite a saved value
   with a fake `1`; a missing output list must not invent `DisplayPort-0`.
6. Serialize changes and coalesce intermediate slider values during fast movement.
   Always deliver the last selected value.
7. Preserve the `output`, `saturation`, and `gamma` keys in `plasmaglowrc`. Validate
   missing or malformed values before applying them.

**Check:** build the applet without the effect; exercise command failures, timeouts,
and request ordering with controlled stub tools; manually check familiar actions in
the current X11 session.

**Complete when:** the X11 path uses the dedicated backend, errors are visible, the
UI does not block, and existing settings still load.

**Implementation progress (2026-09-24):** X11 process handling now lives in
`X11Backend`. Output discovery and value reads are asynchronous with a 1.5 second
timeout; saturation and gamma writes each keep one active process and coalesce
pending slider changes to the latest value. The controller creates this backend
only when Qt reports the `xcb` platform, exposes separate saturation/gamma tool
availability and the latest error, validates persisted values, and ignores stale
read replies. A failed output query keeps the last output list and saved settings;
an empty successful result no longer invents a display name. QML displays backend
errors and missing X11 tools. Both applet and KWin effect targets compile. Live
X11 behavior remains unchecked.

## 7. Phase 4 — connect Wayland to the applet

**Goal:** connect `GlowController` to the PlasmaGlow effect.

1. Add Qt D-Bus and a concrete KWin backend.
2. Asynchronously check KWin availability, whether PlasmaGlow is installed/loaded,
   and its API version. Load the PlasmaGlow effect specifically.
3. Once ready, send the saved pair in one request.
4. Allow at most one active apply request and keep only the latest queued pair. A
   late reply must not override a newer user choice.
5. Read back confirmed state after connecting and on Refresh. Keep this separate
   from explicitly restoring saved user settings at startup.
6. If the interface disappears, show it as unavailable and retain desired values.
   Restore them when the effect returns. Do not poll continuously while connected
   or retry forever after an error.
7. Define how multiple applet instances receive effect-state changes without
   applying signals back in a loop. Do not create independent profiles for one
   global effect.

**Check:** startup with neutral and saved settings; missing plugin; load failure;
incompatible API; endpoint loss and recovery; fast movement of both sliders;
Refresh and Reset.

**Complete when:** the Wayland backend confirms applied state, delivers the latest
value, and recovers without hanging the applet.

**Implementation progress (2026-09-24):** `KWinBackend` asynchronously checks
KWin's Effects interface, loads only the `plasmaglow` effect when needed, validates
API version and state, and sends atomic parameter pairs with one active request
and a latest-value queue. The effect owns the `org.kde.PlasmaGlow` session-bus
service; the applet watches both that service and KWin, so it can detect effect
unload/reload without polling. Desired settings remain separate from confirmed
effect state. Applet instances copy external effect state into their controls
without applying those state signals back; local requests are guarded until their
latest reply. Wayland checks confirmed readiness, API/state readback, saved-setting
application, and restoration after effect unload/reload. Missing-plugin recovery,
incompatible API handling, and fast slider movement still need focused checks.

## 8. Phase 5 — adapt QML

**Goal:** provide the same familiar controls in supported sessions.

1. Replace the generic Wayland prohibition with controls based on backend
   capabilities.
2. Label Wayland behavior as global; keep output selection for X11.
3. Preserve ranges, steps, presets, and number formatting.
4. Make Reset one controller operation that applies `(1, 1)` as a pair.
5. Show a short reason when a feature is unavailable and allow retry through
   Refresh. Keep D-Bus method names and internal paths out of normal UI.
6. Preserve the existing visual style and change only the controls needed for two
   backends and error state.

**Check:** manually use available and unavailable features, presets, reset, Plasma
restart, compact view, and expanded view. Confirm sliders do not jump backward on
late replies and reflect completed application.

**Complete when:** both controls are clear and work in X11 and Wayland.

**Implementation progress (2026-09-24):** controls now use backend capabilities,
the X11 output selector remains X11-only, Wayland is labeled global, errors and
missing X11 tools are surfaced, and Reset applies the Wayland pair atomically.
The user confirmed the Wayland error is gone, gamma works, saturation zero produces
grayscale, and presets work. Reset plus compact/expanded view behavior remain to be
checked manually.

**UI follow-up (2026-09-24):** the user reported clipped Wayland/X11 labels and a
slider that was hard to operate with the mouse. Long labels now wrap to the
available width. Each slider has a transparent mouse area across its full width;
click and drag map directly to the value, while the existing neon visuals and
keyboard handling remain. `qmllint` and the applet build pass; the updated applet
was installed system-wide and `plasmashell` restarted in Wayland. Mouse feel still
needs the user's live confirmation.

## 9. Phase 6 — installation and documentation

**Goal:** build and install the PlasmaGlow effect with the applet.

1. Extend CMake and `install.sh` to install the effect into the KWin plugin
   directory supplied by the installed platform packages.
2. Document build dependencies for the applet and effect, the X11-only build
   option, and the Wayland activation steps.
3. Record the KWin and Qt versions used for acceptance. Native effect plugins rely
   on the KWin ABI and may need rebuilding after an incompatible update.
4. Document how to update a loaded plugin: replacing its file on disk does not
   replace the code already loaded by the KWin process.
5. Update the README and package description with both modes, global Wayland
   correction, settings persistence, and confirmed limitations.
6. Before first activation, check for other active color-correction effects so a
   comparison does not measure two stacked transforms.

**Check:** install into a staging directory and inspect the file list, then install
and activate in the user session. Check KWin effect discovery and Plasma applet
discovery separately.

**Complete when:** documented commands install both components and the user can
repeat installation and upgrade.

**Implementation progress (2026-09-24):** the README documents both backends,
build dependencies, the optional X11-only build, Wayland activation, and the need
to start a new session after replacing a loaded KWin effect. `install.sh` enables
the effect target. Staging and system-wide installs succeeded; KWin and Plasma
discovered their components in a live Wayland session. No further install work is
currently pending.

## 10. Phase 7 — acceptance in a real Wayland session

Use one monitor and check:

- saturation 0 / 1 / 1.5 / 4 and gamma 0.8 / 1 / 1.2, then range boundaries;
- changing each setting independently and together, including operation order;
- neutral pair, every preset, and Reset after changing effect state externally;
- static desktop, wallpaper, panel, regular windows, translucent windows, and menus;
- newly opened windows, fullscreen applications, and switching between them;
- restoring saved settings after logging into the session again;
- disabling the effect, load failure, and connection recovery;
- no extra processing at `(1, 1)` and no noticeable UI delay.

Record actual coverage of the hardware cursor, lock screen, screen capture, and
built-in KWin effects. Processing windows does not guarantee that every such item
is transformed.

Initial visual acceptance is SDR. HDR, wide gamut, Night Light, and user ICC
profiles are not confirmed by SDR results. Check combinations used in practice
separately and document what works.

For visual comparison, use color patches, a gray scale, a photo, and a translucent
window. An X11 screenshot may omit output LUT correction, so screenshots alone do
not prove equivalence with `xgamma`.

**Progress (2026-09-24):** the user confirms Wayland works for normal use: the
backend error is gone, gamma changes the image, saturation zero produces a
grayscale image, presets work, and saved values survive a session restart. The
core Wayland behavior is accepted. Transparent surfaces, HDR/wide-gamut behavior,
and other listed edge cases were not individually checked. The previous X11 path
still needs a separate live regression check.

**Complete when:** the user confirms the image and controls in Wayland. Track the
previous X11 path as a separate regression check. A successful build alone does
not complete either check.

## 11. Execution order and evidence

Order: 1 → 2 → 3 → 4 → 5 → 6 → 7. Phase 2 is the first implementation gate: confirm
the owned effect before making larger controller changes.

After each phase, update this file with what changed, how it was checked, which
versions and sessions were used, and what remains unconfirmed. Expand checks only
for a concrete change or identified risk. Keep general refactoring and unrelated
UI work out of these phases.

| Phase | Status | Evidence |
| --- | --- | --- |
| 1. Rendering point and color math | Source review plus basic SDR visual check | User confirmed gamma and zero saturation; transparency and HDR remain unchecked |
| 2. PlasmaGlow KWin effect | Implemented; smoke-tested in Wayland | KWin 6.7.5, ready API v1; edge and window coverage checks pending |
| 3. Controller and X11 | Implemented; live X11 check pending | Async backend and UI error display compile |
| 4. Wayland backend | Implemented; runtime smoke checks passed | Readiness, saved-setting application, and effect reload recovery confirmed |
| 5. QML | Implemented; partial Wayland acceptance | Error cleared; gamma, saturation zero, and presets confirmed; Reset and view checks pending |
| 6. Installation and documentation | Complete | Staging and system-wide installs succeeded; session restart behavior documented |
| 7. Wayland acceptance | Core behavior accepted; edge coverage limited | User-confirmed gamma, saturation zero, presets, and persistence across a session restart; transparent surfaces and HDR remain unverified |

## Settings persistence check

`GlowController` reads and writes `plasmaglowrc` through KDE's per-user
`KSharedConfig`. Settings changes and Reset queue the affected keys for a write
after 150 ms of inactivity. Pending keys are also written when the controller is
destroyed. A failed `sync()` is reported in the applet and retried.

On this computer, both local accounts have separate files at
`/home/<user>/.config/plasmaglowrc`, owned by the matching account. The active
account's file has mode `644`; the other account's file has mode `600`. This
confirms separate per-account storage paths, not a permission boundary for every
file. The user has confirmed that saved values survive restarting the Wayland
session. Because the same on-disk config path is used in X11 and Wayland, those
sessions share values for one account. A power-loss test and a live X11 check have
not been run.

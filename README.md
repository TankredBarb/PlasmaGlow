# PlasmaGlow

KDE Plasma 6 applet for adjusting color saturation and gamma.

## Backends

- **X11:** uses `vibrant-cli` for saturation on the selected output and `xgamma`
  for gamma. Outputs are detected with `xrandr`.
- **Plasma Wayland:** uses the PlasmaGlow KWin effect for global correction
  across windows and outputs. It does not offer per-output or per-application
  controls.

Both backends keep the saturation and gamma values in the current Linux user's
KDE config file, `plasmaglowrc` under `$XDG_CONFIG_HOME` (normally
`~/.config/plasmaglowrc`). Each account has its own settings; a system-wide
installation shares the applet and effect binaries, not user preferences. The
same account reuses its values across X11 and Wayland sessions, and the applet
applies the saved pair when that session's backend is ready. Changes are saved
after a short delay, and pending changes are saved when the applet closes. The
effect performs correction per window before final composition,
so transparent surfaces may differ from a correction applied to the final
output. Initial shader behavior targets SDR; HDR and wide-gamut behavior are
unconfirmed.

## Requirements

- KDE Plasma 6
- For X11: `vibrant-cli`, `xgamma`, and `xrandr`
- For Wayland: KWin with support for third-party effects

### Build dependencies

- CMake (>= 3.16)
- Extra CMake Modules (ECM)
- Qt 6 Core, DBus, Qml, Quick, and Svg development packages
- KF6 CoreAddons and Config development packages
- Plasma 6 development packages
- To build the KWin effect: KWin development packages and Qt 6 Widgets

## Build and install

The installation script builds and installs both the applet and effect. It needs
the KWin development packages and permission to install under `/usr`:

```bash
chmod +x install.sh
./install.sh
```

To build only the applet, configure with
`-DPLASMAGLOW_BUILD_KWIN_EFFECT=OFF`. This keeps an X11-only build independent of
KWin development headers. To build the full Wayland implementation, use
`-DPLASMAGLOW_BUILD_KWIN_EFFECT=ON`.

If Plasma does not discover the updated applet automatically, restart plasmashell.
KWin loads native effect code into its process; restart the Wayland session after
replacing an already loaded effect binary.

## Add the widget

1. Open **Add Widgets...** from the panel or desktop.
2. Find **PlasmaGlow**.
3. Add it to the panel or desktop.

In a Wayland session, the applet checks for and loads its own KWin effect when it
starts. Use Refresh to retry if the effect is unavailable. The user confirmed in
a live Wayland session that gamma, presets, and grayscale at zero saturation
work, and that saved settings survive a session restart. Additional rendering
edge cases are tracked in the implementation plan.

## Technical details

- `GlowController` owns settings and the QML properties.
- `X11Backend` runs system tools asynchronously, with bounded command timeouts.
- `KWinBackend` uses asynchronous Qt D-Bus calls and talks to the PlasmaGlow
  effect through its own session-bus service.
- The KWin effect uses `OffscreenEffect` and a GLSL shader to adjust each window.

## License

GPL-2.0-or-later (see the package metadata and source headers).

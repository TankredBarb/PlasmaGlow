# PlasmaGlow

KDE Plasma 6 widget for adjusting monitor color saturation and gamma settings under X11.

## Features

- **Monitor selection**: Lists and switches between display outputs detected via `xrandr`.
- **Saturation control**: Adjusts color saturation via `vibrant-cli` (range: `0.0` to `4.0`, default `1.0`). Includes quick presets.
- **Gamma control**: Adjusts monitor gamma via `xgamma` (range: `0.1` to `5.0`, default `1.0`). Appears only if `xgamma` is installed.
- **Settings persistence**: Saves output, saturation, and gamma settings to `~/.config/plasmaglowrc` using `KF6::ConfigCore` and restores them on desktop startup.
- **Wayland warning**: Shows a warning message and hides controls if the session is not X11.

## Requirements

- KDE Plasma 6
- X11 session
- `vibrant-cli`
- `xgamma`
- `xrandr`

### Build Dependencies

- CMake (>= 3.16)
- Extra CMake Modules (ECM) (>= 6.0.0)
- Qt6 (Core, Qml, Quick, Svg)
- KF6 (CoreAddons, Config)
- Plasma 6 development headers

## Installation

The widget compiles a C++ backend plugin and registers the package. The installation must be system-wide under `/usr` so that Plasmashell can load the compiled shared library.

Run the installation script:
```bash
chmod +x install.sh
./install.sh
```
*(Requires sudo to write to `/usr/lib/qt6/plugins/plasma/applets/` and `/usr/share/plasma/plasmoids/`)*

If plasmashell does not load the new plugin automatically, restart it:
```bash
systemctl --user restart plasma-plasmashell
```

## Adding the Widget

1. Right-click on the panel or desktop and select **Add Widgets...**
2. Find **PlasmaGlow**.
3. Drag it to the panel or double-click to add.

## Technical Details

- **Backend**: The `GlowController` class (`src/glowcontroller.cpp`) is registered as a QML element. It calls external utilities (`vibrant-cli`, `xgamma`, `xrandr`) asynchronously using `QProcess`.
- **Persistence**: Configuration is written directly to `~/.config/plasmaglowrc` inside the C++ setter methods and read back during construction, avoiding QML binding loops at startup.

## License

GPL-2.0+ (see `package/metadata.json`).

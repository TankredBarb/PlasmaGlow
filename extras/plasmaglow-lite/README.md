# PlasmaGlow Lite for vedro

This directory preserves the standalone tool originally developed in
`~/vedro-tools`. It is independent of the PlasmaGlow applet and KWin effect:
no Qt/KDE, no Wayland support, and no sharpening. Digital Vibrance requires an
NVIDIA X11 driver exposing NV-CONTROL; gamma uses the external `xgamma` command.

## Build

Build dependencies: a C99 compiler, Xlib development files, `libXNVCtrl`, and
Make or CMake >= 3.19. The NV-CONTROL headers are bundled below `include/`.

From this directory, use `make`, or build independently with CMake:

```sh
cmake -S . -B build
cmake --build build
./build/plasmaglow-lite
```

From the repository root, include it in the normal PlasmaGlow build with:

```sh
cmake -S . -B build -DPLASMAGLOW_BUILD_LITE=ON
cmake --build build
```

The option defaults to OFF. The standalone build does not require the main
project's Qt/KDE dependencies. CMake installation installs both tools and the
desktop launcher; its `Exec=plasmaglow-lite` requires the installed binary on
`PATH`. For a local installation, for example:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

Autostart is configured manually by the desktop user; installation does not
change startup files. Use the installed binary's absolute path with `--apply`.

## Original laptop usage and validation

The notes below describe the original vedro deployment. The two smoke scripts
are manual hardware checks for that machine: they hardcode `DISPLAY=:0`,
`/home/faye/.Xauthority`, DPY-2, and the observed maximum 1023. They expect
Make-built binaries beside the scripts, a running Lite window, Python 3,
`xdotool`, and (for `ui-smoke.py`) `scrot`. Run them only in the matching laptop
session with `XDG_CONFIG_HOME` unset; they change actual display colors.

Small native C/Xlib UI for the HP laptop's X11 desktop. Two sliders,
per-control presets, and a neutral reset. No additional packages were installed.
The window reads actual values when opened. Successfully applied changes are
saved atomically to `$XDG_CONFIG_HOME/plasmaglow-lite.conf`, or
`~/.config/plasmaglow-lite.conf` when XDG_CONFIG_HOME is unset.

```sh
make
./plasmaglow-lite
./plasmaglow-lite --apply
```

`--apply` reads the saved values, applies Vibrance and gamma, and exits without
creating a window, loading fonts, or entering an event loop. A successful run
is silent. With no saved file, it exits successfully before connecting to X.
Invalid settings or failed applies return an error status and write to stderr.
On vedro, the command is registered once in `~/.desktop-session/startup`.
The prior startup script is backed up as `startup.before-plasmaglow-lite` in
the same directory. This restores the values on the next graphical login.

Vibrance uses NVIDIA NV-CONTROL directly, with a slider from 0 to the driver's
maximum (1023 on vedro) and presets 0, 200, 500, 750, Max.
Gamma uses `xgamma`, with a slider from 0.50 to 2.00 and presets 0.8, 1.0, 1.2, 1.5.
Reset applies Vibrance 0 and gamma 1.00.

Use the mouse, wheel, or Tab/Shift+Tab to choose a control. Arrow keys adjust
sliders; Shift increases the step, Home/End select the limits, Enter/Space
activate a preset, and Escape closes the window. Changes are throttled to
one apply per 100 ms while dragging. The process blocks waiting for X events
when idle.

On the laptop, the application is also registered as PlasmaGlow Lite using
`~/.local/share/applications/plasmaglow-lite.desktop`.

The actual-window smoke check is `python3 ui-smoke.py`. It uses the laptop's
existing xdotool/scrot, verifies driver/gamma values, and restores starting
colors, preferences, and the mouse position. Leave the controls alone while it runs.
`python3 restore-smoke.py` checks UI persistence, Reset persistence, invalid
and missing settings, and actual color restoration by `--apply`. It observes
X root-window creation events, including transient windows. On 2026-10-14 it
observed zero windows and a 12.3 ms restore in an already running X session;
graphical-login timing itself has not been measured.

## Command-line diagnostic

Small native NV-CONTROL utility using the installed libX11 and libXNVCtrl.
The included NVIDIA headers come from the nvidia-settings 340.108 tag:
https://github.com/NVIDIA/nvidia-settings/tree/340.108/src/libXNVCtrl
Their license notices remain in the header files.

Build on the laptop:

```sh
cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude vibrance.c -lX11 -l:libXNVCtrl.so.0 -o vibrance
```

Run as the desktop user from a terminal in the X session:

```sh
./vibrance          # Query only; no changes
./vibrance 2 200    # Set DPY-2 to raw value +200
./vibrance 2 0      # Restore the original value observed on this laptop
```

For SSH, prefix the command with:

```sh
DISPLAY=:0 XAUTHORITY=/home/faye/.Xauthority
```

On 2026-10-14, the GeForce 8600M GT with NVIDIA 340.108 reported writable
Digital Vibrance on DPY-2, range -1024..1023, original value 0.
Re-query the display ID and range after changing the display setup.
Values are driver units, not percentages. This utility does not install an
autostart entry or run in the background. Visual quality and performance
are not established by a successful attribute query.

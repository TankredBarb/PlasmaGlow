#!/usr/bin/env python3
"""Verify persistence and window-free restoration on the real laptop."""
import ctypes as c
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

os.environ.update(DISPLAY=":0", XAUTHORITY="/home/faye/.Xauthority", LC_ALL="C")
root = Path(__file__).resolve().parent
config = Path.home() / ".config/plasmaglow-lite.conf"


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, capture_output=True, **kwargs)


def vibrance():
    return int(re.search(r"current=(-?\d+)", run(str(root / "vibrance")).stdout)[1])


def gamma():
    return tuple(float(value) for value in re.findall(
        r"(?:Red|Green|Blue)\s+([\d.]+)", run("xgamma").stderr))


initial_vibrance, initial_gamma = vibrance(), gamma()
previous_config = config.read_text() if config.exists() else None
pointer = run("xdotool", "getmouselocation", "--shell").stdout
pointer_x = re.search(r"^X=(\d+)$", pointer, re.M)[1]
pointer_y = re.search(r"^Y=(\d+)$", pointer, re.M)[1]
window = run("timeout", "5", "xdotool", "search", "--sync", "--onlyvisible",
             "--name", "^PlasmaGlow Lite$").stdout.splitlines()[-1]

# Observe actual root-window creation events during --apply, including transient windows.
x = c.CDLL("libX11.so.6")
x.XOpenDisplay.argtypes, x.XOpenDisplay.restype = [c.c_char_p], c.c_void_p
x.XDefaultRootWindow.argtypes, x.XDefaultRootWindow.restype = [c.c_void_p], c.c_ulong
x.XSelectInput.argtypes = [c.c_void_p, c.c_ulong, c.c_long]
x.XSync.argtypes = [c.c_void_p, c.c_int]
x.XPending.argtypes = [c.c_void_p]
x.XNextEvent.argtypes = [c.c_void_p, c.c_void_p]
x.XCloseDisplay.argtypes = [c.c_void_p]
display = x.XOpenDisplay(None)
assert display
x.XSelectInput(display, x.XDefaultRootWindow(display), 1 << 19)
x.XSync(display, 0)

try:
    with tempfile.TemporaryDirectory() as empty:
        environment = dict(os.environ, XDG_CONFIG_HOME=empty, DISPLAY="")
        result = run(str(root / "plasmaglow-lite"), "--apply", env=environment)
        assert not result.stdout and not result.stderr
        print("PASS: missing settings exits silently without an X connection", flush=True)
        bad = Path(empty) / "plasmaglow-lite.conf"
        for contents in ["broken\n", "vibrance=1024\ngamma=1.00\n", "vibrance=500\ngamma=nan\n"]:
            bad.write_text(contents)
            result = subprocess.run([str(root / "plasmaglow-lite"), "--apply"],
                                    env=environment, capture_output=True)
            assert result.returncode != 0
        print("PASS: invalid settings are rejected before connecting to X", flush=True)

    for x_position, y_position in [(300, 120), (270, 244)]:
        run("xdotool", "mousemove", "--window", window, str(x_position), str(y_position), "click", "1")
        time.sleep(0.2)
    assert config.read_text() == "vibrance=750\ngamma=1.20\n", config.read_text()
    assert vibrance() == 750 and all(abs(value - 1.2) < 0.011 for value in gamma())
    print("PASS: UI saves both successfully applied values", flush=True)

    run(str(root / "vibrance"), "2", "0")
    run("xgamma", "-gamma", "1")
    event = (c.c_long * 24)()
    while x.XPending(display):
        x.XNextEvent(display, c.byref(event))
    started = time.perf_counter()
    result = run(str(root / "plasmaglow-lite"), "--apply")
    elapsed = (time.perf_counter() - started) * 1000
    assert not result.stdout and not result.stderr
    assert vibrance() == 750 and all(abs(value - 1.2) < 0.011 for value in gamma())
    x.XSync(display, 0)
    created = 0
    while x.XPending(display):
        x.XNextEvent(display, c.byref(event))
        if c.cast(c.byref(event), c.POINTER(c.c_int))[0] == 16:
            created += 1
    assert created == 0, created
    print(f"PASS: --apply restores both values silently, creates {created} windows, exits in {elapsed:.1f} ms", flush=True)

    run("xdotool", "mousemove", "--window", window, "84", "292", "click", "1")
    time.sleep(0.2)
    assert config.read_text() == "vibrance=0\ngamma=1.00\n"
    print("PASS: Reset saves neutral values", flush=True)
finally:
    x.XCloseDisplay(display)
    subprocess.run(["xdotool", "key", "--window", window, "Escape"], capture_output=True)
    run("xdotool", "keyup", "Escape")
    run(str(root / "vibrance"), "2", str(initial_vibrance))
    run("xgamma", "-rgamma", str(initial_gamma[0]), "-ggamma", str(initial_gamma[1]),
        "-bgamma", str(initial_gamma[2]))
    config.write_text(previous_config if previous_config is not None else
                      f"vibrance={initial_vibrance}\ngamma={initial_gamma[0]:.2f}\n")
    run("xdotool", "mousemove", pointer_x, pointer_y)
    print("Original colors and saved preferences restored", flush=True)

#!/usr/bin/env python3
"""Exercise the real X11 window and verify both hardware controls; restore colors."""
import os
from pathlib import Path
import re
import subprocess
import time

os.environ.update(DISPLAY=":0", XAUTHORITY="/home/faye/.Xauthority", LC_ALL="C")
root = os.path.dirname(os.path.abspath(__file__))


def run(*args):
    return subprocess.run(args, check=True, text=True, capture_output=True)


def vibrance():
    return int(re.search(r"current=(-?\d+)", run(root + "/vibrance").stdout)[1])


def gamma():
    result = run("xgamma")
    return tuple(float(value) for value in re.findall(
        r"(?:Red|Green|Blue)\s+([\d.]+)", result.stderr))


window = run("xdotool", "search", "--name", "^PlasmaGlow Lite$").stdout.splitlines()[-1]
original_vibrance, original_gamma = vibrance(), gamma()
config = Path.home() / ".config/plasmaglow-lite.conf"
original_config = config.read_text() if config.exists() else None
pointer = run("xdotool", "getmouselocation", "--shell").stdout
pointer_x = re.search(r"^X=(\d+)$", pointer, re.M)[1]
pointer_y = re.search(r"^Y=(\d+)$", pointer, re.M)[1]
print(f"Starting values: vibrance={original_vibrance}, gamma={original_gamma}", flush=True)


def click(x, y):
    run("xdotool", "mousemove", "--window", window, str(x), str(y), "click", "1")
    time.sleep(0.2)


try:
    for x, value in [(60, 0), (140, 200), (220, 500), (300, 750), (380, 1023)]:
        click(x, 120)
        assert vibrance() == value, ("vibrance preset", value, vibrance())
    for x, value in [(70, 0.8), (170, 1.0), (270, 1.2), (370, 1.5)]:
        click(x, 244)
        assert all(abs(actual - value) < 0.011 for actual in gamma()), ("gamma preset", value, gamma())
    print("PASS: all nine preset buttons", flush=True)
    run("scrot", "--overwrite", "-u", root + "/ui-smoke.png")
    click(220, 82)
    assert abs(vibrance() - 512) <= 2
    click(220, 206)
    assert all(abs(actual - 1.25) < 0.011 for actual in gamma())
    print("PASS: both slider positions", flush=True)
    run("xdotool", "mousemove", "--window", window, "220", "82", "mousedown", "1",
        "mousemove", "--window", window, "409", "82", "mouseup", "1")
    time.sleep(0.2)
    assert vibrance() == 1023
    print("PASS: slider dragging to maximum", flush=True)
    click(84, 292)
    assert vibrance() == 0 and all(abs(actual - 1) < 0.011 for actual in gamma())
    print("PASS: reset restores both neutral values", flush=True)
    click(31, 82)
    run("xdotool", "key", "--window", window, "Right")
    time.sleep(0.2)
    assert vibrance() == 10
    print("PASS: keyboard slider adjustment", flush=True)
finally:
    # Escape destroys the window before xdotool can send its key release.
    subprocess.run(("xdotool", "key", "--window", window, "Escape"),
                   capture_output=True, check=False)
    run("xdotool", "keyup", "Escape")
    time.sleep(0.15)
    run(root + "/vibrance", "2", str(original_vibrance))
    run("xgamma", "-rgamma", str(original_gamma[0]), "-ggamma", str(original_gamma[1]),
        "-bgamma", str(original_gamma[2]))
    if original_config is not None:
        config.write_text(original_config)
    elif config.exists():
        config.unlink()
    run("xdotool", "mousemove", pointer_x, pointer_y)
    print("Original colors restored", flush=True)

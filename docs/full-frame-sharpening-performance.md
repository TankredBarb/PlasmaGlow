# Full-frame sharpening measurements

2026-10-03. Target: SDR, 1920×1080 and 2560×1440, with eventual game use.
No game or live compositor settings were changed for these measurements.

## Tiled compute prototype

A 16×16 compute workgroup cooperatively loads and decodes an 18×18 input
neighbourhood into shared FP32 memory. CAS/Luma then use those decoded values.
For a complete tile this replaces 2,304 source fetches and decodes with 324,
without a full-size intermediate texture or an additional rendering pass.
Values stay in nits until the existing filter normalization, preserving its
operation order. Every invocation reaches the shared-memory barrier before
out-of-bounds output invocations return.

Hardware: RX 6600 XT, radeonsi/ACO, Mesa 26.2.2, OpenGL 4.6.
Settings: gamma 0.9, saturation 2, strength 0.5, denoise 0.34.
Times below are median GPU milliseconds for RGBA16F input/output. Compute times
include dispatch and framebuffer, texture-fetch and image-access barriers.
The fixture is deterministic opaque noise. Each case has 80 warmup iterations
and 101 timed batches of four complete passes.

| Resolution | Encoding | Filter | Fragment | Compute 16×16 |
|---|---|---|---:|---:|
| 1080p | linear | CAS | 0.137 | 0.131 |
| 1080p | linear | Luma | 0.120 | 0.118 |
| 1080p | sRGB | CAS | 0.305 | 0.175 |
| 1080p | sRGB | Luma | 0.287 | 0.156 |
| 1080p | gamma22 | CAS | 0.265 | 0.165 |
| 1080p | gamma22 | Luma | 0.244 | 0.147 |
| 1440p | linear | CAS | 0.246 | 0.284 |
| 1440p | linear | Luma | 0.230 | 0.276 |
| 1440p | sRGB | CAS | 0.548 | 0.325 |
| 1440p | sRGB | Luma | 0.517 | 0.304 |
| 1440p | gamma22 | CAS | 0.474 | 0.314 |
| 1440p | gamma22 | Luma | 0.439 | 0.291 |

The prototype reduces nonlinear RGBA16F pass time by roughly 34–46% in these
cases, but regresses linear RGBA16F in 1440p by roughly 16–20%. Workgroups of
8×8 were slower than 16×16 for the nonlinear cases.

GPU image comparison against the current optimized fragment shader passed
20,736 cases with zero observed difference in FP32 readback. Inputs included
RGBA8, RGBA16F and RGBA32F; sRGB, linear, gamma22 and BT1886 transfers; opaque,
transparent, noisy, checkerboard and gradient pixels; small/odd dimensions;
and the endpoints of gamma, strength, denoise and saturation settings used by
the probe. Another 20,736 cases per output format checked RGBA8 and RGBA16F
storage. Those differed by at most one quantization step: one byte value in
RGBA8, or one adjacent half-float value in RGBA16F (maximum absolute difference
0.000488281). Thus the FP32 results match, but the stored low-precision results
are not bit-identical across fragment and image-store paths.

## Integrated render path

The effect now selects tiled compute for full-target CAS/Luma passes on
nonlinear sRGB/gamma22 SDR captures, using desktop OpenGL 4.3 or newer and
RGBA8/RGBA16/RGBA16F storage. The scene still renders once into its existing capture;
compute writes straight to the output texture without an extra full-size
intermediate or copy pass. Fragment and compute share the filter and adjustment
formulas in `plasmaglow-sharpening.glsl`.

Linear captures retain the fragment path because compute regressed the
RGBA16F 1440p benchmark. Other formats/encodings, rotated outputs, partial
regions, offsets, nonmatching target sizes and raster states that image stores
cannot reproduce also use the fragment path. Normal/FlipY outputs are supported;
projected quad corners must match the complete target, including scale and
logical output origin. Unsupported GL or a compute compile failure falls back
without disabling sharpening. HDR continues to bypass sharpening.

The compute helper restores the previous program, active texture unit, texture
binding and image binding, and issues framebuffer, texture-fetch and image-access
barriers. It does not change KWin's ShaderManager stack. Compilation is attempted
once per effect instance, on the first eligible pass.

The `plasmaglow.render` logging category reports capture size, format, transfer,
reference luminance, scale, transform and the selected pass when a new capture
is allocated. Debug output is disabled by default. Enable it through
`QT_LOGGING_RULES=plasmaglow.render.debug=true` when starting a test compositor;
use `QT_FORCE_STDERR_LOGGING=1` to capture Qt messages on stderr if necessary.

Isolated virtual KWin runs loaded the built effect and selected compute for
CAS at 1920×1080 and Luma at 2560×1440. Both reported GL_RGBA8 (`0x8058`),
gamma22, reference luminance 80, scale 1 and FlipY. A separate run with Mesa's
GL version restricted to 3.3 selected the fragment fallback. These runs used
private config/runtime directories and a private D-Bus session, without
changing the live compositor or running games.

The regression target `plasmaglow-sharpening-gpu-test` uses the actual KWin
fragment program, color uniforms, projection matrix and production compute
helper. It passed 3,456 image comparisons on RX 6600 XT: RGBA8/RGBA16F,
sRGB/gamma22, CAS/Luma, Normal/FlipY, small and incomplete tiles, transparency,
noise, gradients, endpoint controls, fractional scale and a nonzero output
origin. Maximum difference was one adjacent storage value. It also checks
program/texture/image state restoration and blend/scissor fallback.
Build with both `PLASMAGLOW_BUILD_KWIN_EFFECT=ON` and
`PLASMAGLOW_BUILD_TESTS=ON`, then run:

```sh
ctest --test-dir build -R sharpening-gpu --output-on-failure
```

An unavailable surfaceless EGL display or GL version below 4.3 skips the GPU
test, rather than establishing parity. The complete local suite passed 3/3.

The final integrated shader sources were also measured with the standalone
GPU benchmark, sequentially to avoid overlap between A/B runs. For the RGBA8,
gamma22 configuration found in virtual KWin, median GPU milliseconds were:

| Resolution | Filter | Fragment | Compute 16×16 | Reduction |
|---|---|---:|---:|---:|
| 1080p | CAS | 0.272 | 0.162 | 40% |
| 1080p | Luma | 0.253 | 0.144 | 43% |
| 1440p | CAS | 0.467 | 0.291 | 38% |
| 1440p | Luma | 0.435 | 0.258 | 41% |

This standalone benchmark includes GPU dispatch/barriers but excludes the
compute helper's CPU state queries/restoration and complete compositor work.

These timings exclude scene composition, concurrent game load and display
presentation. They establish a promising shader implementation, not an FPS or
latency improvement in a game. The capture redirects the existing composition
into an offscreen framebuffer; it is not an additional explicit screen copy.

## Live Wukong Benchmark A/B

2026-10-03, after installing the effect and starting a fresh Wayland session.
Black Myth: Wukong Benchmark Tool, Steam AppID 3132990, game version
1.0.3.14649; Ryzen 5 5500, RX 6600 XT (RADV), 16 GB RAM, 8 GB VRAM.
The benchmark reports 1920×1080 output, `QualityLevel=3`, `ImageQuality=65`,
DX12 enabled, ray tracing off and built-in frame generation off.
All recorded graphics settings match between the compared runs.

Steam's actual launch log confirms these launch options for both runs:

```sh
DISABLE_LSFGVK=1 DISABLE_MANGOHUD=1 DISABLE_VKBASALT=1 %command%
```

No FrameFlow or MangoHud launcher wraps the Proton command. The user reported
good subjective behavior in the clean Wayland setup.

| Benchmark metric | Neutral | Color + CAS | Difference |
|---|---:|---:|---:|
| Reported average FPS | 72 | 70 | -2 (-2.8%) |
| Reported minimum FPS | 37 | 34 | -3 |
| Reported maximum FPS | 82 | 82 | 0 |
| Reported FPS95 field | 65 | 64 | -1 |
| Median game GPU frame time | 12.743 ms | 12.993 ms | +0.250 ms |
| p95 game GPU frame time | 14.155 ms | 14.423 ms | +0.268 ms |
| Median game CPU frame time | 7.292 ms | 7.386 ms | +0.094 ms |

Neutral parameters: gamma 1, saturation 1, sharpening Off. Effect parameters:
gamma 0.9, saturation 2, CAS strength 0.5, denoise 0.34; set before the run
and confirmed via D-Bus afterward. Neutral history ID: 1791028615 (10,182
records over 141.675 s). Effect history ID: 1791028992 (9,923 records over
141.658 s). Results originate from Proton's
`AppData/Local/Temp/b1/BenchMarkHistory/Tool/<history ID>` JSON files.
Percentiles above are calculated from the recorded game frame-time samples.
The benchmark's `FPS95` field is reproduced without assuming its definition.

An earlier neutral run also reported 72 average FPS, with median game GPU
time 12.757 ms. Its launch options contained a malformed `%command` placeholder;
use the corrected neutral repeat as the comparison baseline.

This pair suggests a modest total cost for active color correction plus CAS
under concurrent game load. Game GPU frame time does not directly measure the
compositor pass: shared GPU contention and composition/scanout changes can
contribute. This is not a compute-versus-fragment comparison, does not verify
the live selected render path, and one effect run cannot establish repeatability
or behavior with lsfg-vk enabled. Minimum FPS also varied between neutral runs
(33 and 37), so the single minimum should not be used to attribute a new stall.

## Live render-path verification

A subsequent temporary kernel uprobe trace of the loaded plugin confirmed that
the fresh live KWin process (PID 41069, installed/loaded inode 1717702) currently
selects the fragment path on the desktop. All sampled decisions showed
`computed=0`, transfer gamma22 (3), format `GL_RGBA16` (`0x805b`), CAS (mode 1)
and FlipY (6); the compute dispatch helper was not entered. GL_RGBA16 is a
normalized 16-bit SDR storage format, distinct from GL_RGBA16F. The compute
selector currently accepts only GL_RGBA8 and GL_RGBA16F, so this is a deliberate
format fallback. Temporary probe events and their private trace instance were
removed after sampling. No KWin restart or debugger pause was used.

This desktop trace does not retrospectively prove the selected path during the
Wukong run. It establishes that the live configuration differs from the virtual
KWin's RGBA8 configuration; the Wukong A/B must not be presented as measured
compute performance. The subsequent GL_RGBA16 extension and its validation are described below.

## GL_RGBA16 compute support

The selector now accepts GL_RGBA16 for the same nonlinear, full-target SDR
conditions as RGBA8/RGBA16F. The compute shader and image-store binding already
support this normalized format, so no filter formulas or capture precision
were changed. Linear captures continue to use fragment.

The GPU regression now includes GL_RGBA16 input/output. UNORM16 output is read
as GL_UNSIGNED_SHORT and compared in integer storage steps; it is not converted
to half-float for the comparison. All 5,184 comparisons passed on RX 6600 XT,
with a maximum difference of one storage step across the three formats. The
full CTest suite passed 3/3, and an isolated KWin validated the additive D-Bus
render-output diagnostic and compute selection on its RGBA8 target.

Sequential standalone GPU timings for GL_RGBA16/gamma22, with the same settings
and warmup/batch method as above:

| Resolution | Filter | Fragment | Compute | Reduction |
|---|---|---:|---:|---:|
| 1080p | CAS | 0.277 ms | 0.166 ms | 40% |
| 1080p | Luma | 0.258 ms | 0.148 ms | 43% |
| 1440p | CAS | 0.479 ms | 0.315 ms | 34% |
| 1440p | Luma | 0.443 ms | 0.293 ms | 34% |

These GPU shader timings exclude the helper's CPU state handling and concurrent
game load. Live use of the updated selector requires KWin to load the newly
installed library; the preceding desktop trace sampled the previous version.

`getState` now exposes `renderOutputs`, containing output name,
`lastRenderPath` (compute/fragment), `lastSharpeningMode` (0/1/2),
`captureInternalFormat` and `captureTransferFunction`. These describe the last
observed pass, not a prediction from the current parameters. A newly allocated
capture does not report an observed path until a pass has been selected. No
per-frame D-Bus signal or GL query is added for this diagnostic.

```sh
qdbus6 org.kde.PlasmaGlow /org/kde/PlasmaGlow org.kde.plasmaglow.Effect1.getState
```

After a fresh Wayland login, with CAS enabled, confirm `lastRenderPath=compute`,
mode 1, GL_RGBA16 (32859) and gamma22 (3) before the next Wukong comparison.
Separately measuring color correction is deferred until compute is validated
in the working session.

## Updated live session confirmation

After the next logout/login, the production effect's D-Bus `getState` reports
DP-3, `lastRenderPath=compute`, CAS (`lastSharpeningMode=1`), GL_RGBA16
(`captureInternalFormat=32859`) and gamma22 (`captureTransferFunction=3`).
Parameters are gamma 0.9, saturation 2, strength 0.5 and denoise 0.34, with no
reported error. This confirms the updated path on the working desktop. The
Steam launch options remain the verified disable-layer command above; the
latest completed Wukong result before the next run remains history 1791028992
(70 reported average FPS). Confirmation during the game and the new result
are still pending.

## Wukong result after enabling compute for GL_RGBA16

The user manually launched the next run through Steam after a fresh Wayland
login. The launch log again shows the verified disable-layer command, and all
recorded benchmark graphics settings match the prior neutral/effect runs.
D-Bus reports compute with CAS on GL_RGBA16/gamma22 both before and after the
run; render selection was not traced continuously during the benchmark.
Parameters remained gamma 0.9, saturation 2, CAS 0.5, denoise 0.34.

| Benchmark metric | Neutral | Earlier color + CAS | Updated color + CAS |
|---|---:|---:|---:|
| Reported average FPS | 72 | 70 | 71 |
| Reported minimum FPS | 37 | 34 | 33 |
| Reported maximum FPS | 82 | 82 | 84 |
| Reported FPS95 field | 65 | 64 | 64 |
| Median game GPU frame time | 12.743 ms | 12.993 ms | 12.930 ms |
| p95 game GPU frame time | 14.155 ms | 14.423 ms | 14.334 ms |
| Median game CPU frame time | 7.292 ms | 7.386 ms | 7.363 ms |

New history ID: 1791030405, 10,044 records over 141.667 seconds. Relative to
neutral, reported average FPS is lower by 1 (-1.4%) and median game GPU time
higher by 0.188 ms. Relative to the earlier CAS run, reported average FPS is
higher by 1 and median game GPU time lower by 0.063 ms (p95 lower by 0.089 ms).
These are observations from single passes across different Wayland sessions,
not an estimate with repeated-run uncertainty or a direct compositor timing.
A color-only pass (gamma 0.9, saturation 2, sharpening Off) is the next planned
comparison to separate correction from the total active effect cost.

## Color-only Wukong comparison

History 1791030806 contains the color-only run: gamma 0.9, saturation 2,
sharpening Off, 10,091 records over 141.671 seconds. The actual Steam command
again uses the verified disable-layer options, all recorded graphics settings
match, and post-run D-Bus reports fragment with sharpening mode 0 on GL_RGBA16/
gamma22. The previous updated CAS and this color-only run share the restarted
Wayland session; the neutral and earlier CAS runs preceded that logout/login.

| Benchmark metric | Neutral | Color only | Earlier color + CAS | Updated color + CAS |
|---|---:|---:|---:|---:|
| Reported average FPS | 72 | 71 | 70 | 71 |
| Median game GPU frame time | 12.743 ms | 12.858 ms | 12.993 ms | 12.930 ms |
| p95 game GPU frame time | 14.155 ms | 14.282 ms | 14.423 ms | 14.334 ms |
| Median game CPU frame time | 7.292 ms | 7.309 ms | 7.386 ms | 7.363 ms |

In these samples, color-only adds 0.115 ms to median game GPU time relative to
neutral (p95 +0.127 ms). Updated color + CAS adds another 0.072 ms relative to
color-only (p95 +0.052 ms). The benchmark rounds both latter average FPS values
to 71. These differences are not isolated shader costs: activating correction
also changes scene composition/direct-scanout eligibility, and the game timings
observe shared GPU load rather than the KWin pass directly. Single passes cannot
separate repeatability, session differences and background/thermal variation
from the small incremental effects. No conclusions about lsfg-vk integration
follow from these tests, since its layer was disabled.

After completing this diagnostic series, parameters are restored to gamma 0.9,
saturation 2, CAS strength 0.5 and denoise 0.34.

## Final neutral / color / CAS cycle in one live session

The requested final cycle used identical recorded game settings and verified
Steam launch options, within the same updated Wayland session. CAS parameters
remained gamma 0.9, saturation 2, strength 0.5, denoise 0.34. Compute on GL_RGBA16/
gamma22 was reported before and after the CAS run, without continuous tracing.

| Benchmark metric | Neutral | Color only | Color + CAS compute |
|---|---:|---:|---:|
| Reported average FPS | 71 | 71 | 71 |
| Median game GPU frame time | 12.778 ms | 12.850 ms | 12.921 ms |
| p95 game GPU frame time | 14.178 ms | 14.269 ms | 14.322 ms |
| Reported minimum FPS | 37 | 37 | 35 |
| Reported maximum FPS | 82 | 83 | 85 |
| Median game CPU frame time | 7.232 ms | 7.288 ms | 7.335 ms |

History IDs: neutral 1791031281 (10,152 records), color 1791031617 (10,100
records), CAS 1791031934 (10,047 records); durations 141.662 / 141.678 /
141.667 seconds. Color-only changes median game GPU time by +0.072 ms relative
to neutral; CAS changes it by another +0.071 ms, for +0.144 ms total (+1.12%).
Corresponding p95 differences are +0.090 / +0.053 / +0.144 ms. These are observed
game timing differences, not direct compositor-pass costs or a statistical
confidence bound. The CAS median is close to its earlier updated-session run
(12.930 ms), and its incremental median over color-only is also close to the
earlier observation (+0.072 ms). Average FPS is rounded to the same integer
across all final-cycle modes; minimum FPS differences remain inconclusive.

The neutral/color/CAS cycle is complete. At the user's subsequent request,
two additional Luma passes used gamma 0.9, saturation 2, strength 0.5 and
denoise 0.34, with the same benchmark settings and disable-layer launch command.

## Final Luma repeat and completed comparison

Both additional runs match the recorded graphics settings of the final cycle.
The Steam launch log confirms the disable-layer command, and D-Bus reports
Luma compute on GL_RGBA16/gamma22 before and after the runs; this is an observed
desktop render path, without continuous tracing during the benchmark.

| Benchmark metric | Neutral | Color only | Color + CAS | Color + Luma 1 | Color + Luma 2 |
|---|---:|---:|---:|---:|---:|
| Reported average FPS | 71 | 71 | 71 | 71 | 71 |
| Median game GPU frame time | 12.778 ms | 12.850 ms | 12.921 ms | 12.903 ms | 12.887 ms |
| p95 game GPU frame time | 14.178 ms | 14.269 ms | 14.322 ms | 14.306 ms | 14.318 ms |

Luma history IDs are 1791032271 (10,057 records over 141.658 seconds) and
1791032556 (10,062 records over 141.671 seconds). Their median game GPU times
differ by 0.016 ms. Relative to color-only, the observed increments are 0.053
and 0.037 ms; relative to neutral, 0.125 and 0.109 ms. Luma medians are 0.018
and 0.035 ms below CAS, while p95 values remain close. These small differences
do not establish a statistically reliable performance advantage for either
sharpening mode; image preference is a reasonable basis for choosing between
them in this tested setup.

The requested measurements are complete. Current settings remain Luma 0.5,
denoise 0.34, gamma 0.9, saturation 2. These 1080p game timings indicate a small
increment under the tested clean setup, not isolated compositor-pass costs or
results with lsfg-vk enabled.

## Review regression coverage

The GPU comparison now overwrites the destination with deliberately different
storage values after reading the fragment reference and before compute. Every
channel, including alpha, is poisoned with either zero or one opposite the
reference's half-range. Missing image stores therefore cannot retain the
reference or accidentally pass on transparent black pixels. A temporary copy
with `glDispatchCompute` removed fails immediately with a pixel mismatch;
the previous test passed all comparisons with that same omission.

The effect and GPU test now share the existing selector and screen-quad draw
code in `screenpass.cpp`. Selector coverage includes full Normal/FlipY targets,
fractional scale and output origin, Off, partial/empty damage, size and projected
quad mismatch, render offset, other output transforms, unsupported formats and
transfers. Compute fallback checks cover blend, scissor, depth, stencil,
rasterizer discard, culling, logic operations, viewport offset/size and each
disabled color channel. Texture unit zero restoration is also checked.

The actual color-only shader and screen-pass draw helper are exercised in 48
pixel-level cases: all eight output transforms, empty/partial/full damage and
scissoring initially enabled or disabled. The oracle independently inverts the
output transform per pixel and verifies both pixels outside the damage region
and intersection with the existing scissor box, as well as GL state restoration.
These are isolated GPU tests of the production helpers, not a complete KWin
effect-chain integration test.

## RCAS follow-up: two live Wukong runs

2026-10-07. RCAS strength 0.5 now reaches the native RCAS maximum; the upper
half of the widget slider adds the same 1–4x linear-light overdrive as CAS.
Both measured runs used gamma 0.9, saturation 2 and strength 0.5. The stored
denoise value was 0.17, but RCAS does not use it. Its optional noise suppression
is disabled. No additional neutral, color-only, CAS or Luma measurements were
requested for this follow-up; the earlier warmup is excluded from this table.

The loaded KWin library's inode matched the installed plugin before the runs.
D-Bus reported RCAS compute on GL_RGBA16/gamma22 before, between and after the
runs, without continuous render-path tracing. Steam's actual launch log confirms
`DISABLE_LSFGVK=1 DISABLE_MANGOHUD=1 DISABLE_VKBASALT=1` for both launches.
The two JSON histories have identical non-result metadata. Their documented
settings match the earlier comparison: 1920x1080, QualityLevel 3, ImageQuality
65, DX12, no ray tracing or built-in frame generation, game version 1.0.3.14649,
Ryzen 5 5500 and RX 6600 XT.

| Mode | Date | Reported average FPS | Median game GPU, ms | p95 game GPU, ms |
|---|---|---:|---:|---:|
| Color + CAS | 2026-10-03 | 71 | 12.921 | 14.322 |
| Color + Luma 1 | 2026-10-03 | 71 | 12.903 | 14.306 |
| Color + Luma 2 | 2026-10-03 | 71 | 12.887 | 14.318 |
| Color + RCAS 1 | 2026-10-07 | 71 | 12.926 | 14.335 |
| Color + RCAS 2 | 2026-10-07 | 70 | 12.928 | 14.359 |

RCAS history IDs are 1791377632 (10,026 records over 141.672 seconds) and
1791377937 (10,020 records over 141.670 seconds). Results were read from
`steamapps/.compatdata-users/1000/3132990/pfx/drive_c/users/steamuser/AppData/Local/Temp/b1/BenchMarkHistory/Tool/<history ID>`.
Median game CPU times are 7.175 / 7.094 ms; minimum FPS 33 / 34, maximum FPS
81 / 83, and the reported FPS95 field is 64 in both runs. RCAS p95 values use
linear interpolation at the 95th percentile of the recorded GPU times.
Raw histories and extracted metrics were preserved under
`/tmp/plasmaglow-wukong-rcas-2026-10-07/`.

The two RCAS GPU medians differ by 0.002 ms and lie 0.005–0.007 ms above the
earlier CAS median. This is consistent with a comparable overall performance
level in this workload, but the measurements come from different days and
Wayland sessions. They do not isolate the incremental RCAS pass cost, establish
a reliable advantage over CAS/Luma, or quantify loss relative to a contemporary
neutral baseline. The reported average FPS differs by one integer while GPU
medians remain close. These remain game timings under concurrent compositor
load, not isolated shader timings or results with lsfg-vk enabled.

The reviewed implementation passed all three CTest targets on RX 6600 XT,
including 7,776 fragment/compute comparisons with at most one storage-step
difference. RCAS's independent analytic fixtures now exercise the fixed lobe
cap, both adaptive clipping limits, black/white flat fields, alpha and all five
strength presets (25 cases). Persistence, paused restore, mode transitions and
Reset are also covered. These checks establish implementation correctness on
the fixtures; live visual preference remains the user's assessment.

# Automation lanes, live capture, markers, memory locations, big counter (HUSH 2.2.0)

## What was translated vs written new (read this first)
- **Translated to C++ from your soundcraft-main.zip** (`crates/model/src/automation.rs`, `markers.rs`; SoundCraft is MIT OR Apache-2.0, notice in third_party/soundcraft_notice/): lane model, the seven automation modes, parameter ids (`volume`, `pan`, `pan2`, `mute`, `send_a_level|pan|mute`, `plugin:a:<param>`), value ranges, linear interpolation with step behaviour for mute lanes, `set_point` clamping, `write_range`, `thin`, `shift_from`, memory locations. The Rust unit tests were translated too.
- **Written new (no Rust source existed)**: live Write / Touch / Latch capture (`recorder.*`), the big-counter formatter, session serialization of lanes/markers, and render-time application of lanes. SoundCraft's zip stores the *mode* but contains no capture logic, so the rules below are my implementation of standard console behaviour, not a port.
- **Not ported on purpose**: the `soundcraft-automation` crate (MCP server and control channel). Your spec excludes remote-control mechanisms. The GitHub repo you linked (storytold/soundcraftAutomation) returns 404 (private or misnamed), and the crate of that name in your zip is that remote-control server.

## Live capture rules (src/recorder.hpp)
| Mode | Behaviour |
|---|---|
| Write | Writes from the start of the pass to its end whether or not the control is touched. Outside the written span the original curve is kept. |
| Touch | Writes only while the control is touched. On release the lane returns to its original curve, as a step or over `--return` samples. |
| Latch | Starts at the first touch, keeps writing the last value after release, until the pass ends. |
| Touch/Latch | Faders (volume, pan, send level/pan) behave like Touch; switches (mute, send mute) and plugin parameters like Latch. (My reading of the usual convention; SoundCraft does not define it. Say if you want something else.) |
| Read / Off | No capture. Read plays lanes back; Off ignores lanes. |
| Trim | **Live Trim capture is not implemented** (events are ignored). |
Points are thinned while recording (default: skip changes under 0.01 units unless 480 samples have passed). Anchor points are placed around each written span so the rest of the curve is untouched.
The recorder allocates, so it must be driven from the engine/UI thread, never from the real-time audio callback. There is **no real-time glue yet**: with no audio backend or GUI, nothing feeds it live. `hush lane-record` replays a recorded event stream (`begin AT` / `u AT VALUE 0|1` / `end AT`) through it, so the behaviour is fully testable offline.

## What is applied at render time
Volume and mute lanes are applied per frame by `session-render` (volume ramps linearly in dB; mute steps at >= 0.5; mode Off ignores lanes). **Pan, send and plugin-parameter lanes are stored, saved, edited and undoable but NOT rendered**, because there is no panner, send bus or plugin host yet.

## Markers / memory locations
Marker (point), Selection (range), None (view settings only); numbers are unique and the lowest free number is reused; rulers 1-5; optional colour, comments and track; next/previous navigation and recall. Track groups from the same Rust file were not ported.

## Big counter
`hush counter SAMPLE [--sr N --bpm B --fps ...]` prints timecode (incl. 29.97 drop-frame), bars|beats|ticks, h:mm:ss.mmm and samples for one position. There is no GUI window; this is the formatter a window would call. Tempo is a single constant BPM in the CLI (the library supports tempo maps).

## Limits and honesty
Session files cap at 4 MiB, 512 lanes per track, 200,000 points per lane and 1,000 memory locations. All numbers/ids are validated on load (tests include malformed files). Evidence: evidence/lanes_tests.txt (47 checks), evidence/cli_smoke.txt (53 steps), sanitizer runs in evidence/asan_ubsan_lanes.txt.

# HUSH Processor — Build Reconciliation Report

## Canonical result

This package uses **HUSH 2.2.0 release-gate** as the canonical active source tree. It is the newest supplied source build and the only supplied version with the dedicated release-gate script and release-gate documentation. It contains all relative file paths found in the supplied HUSH 2.1.0 and HUSH 2.2.0 early-access trees. The older builds are preserved byte-for-byte under `provenance/original-archives/` for auditability; they are not overlaid on newer source files because that would silently regress fixes.

## Inputs reconciled

| Source archive | Role in reconciliation |
|---|---|
| `Hush-2.0.0.zip` | Historical source, technical manual and earlier evidence; retained as original archive, with its Technical Manual separately exposed under `docs/history/`. |
| `HUSH-2.0.0(1).zip` | Alternate 2.0.0 package snapshot; retained separately because it is not byte-identical to the other 2.0.0 archive. |
| `HUSH-2.1.0.zip` | Baseline with source, tests and scripts; all of its source-tree paths are present in the canonical 2.2.0 tree. |
| `HUSH-2.2.0-early-access.zip` | Intermediate 2.2.0 build; its paths are present in the canonical tree. |
| `HUSH-2.2.0-release-gate.zip` | Canonical source tree; adds `RELEASE_GATE.md`, `scripts/release_gate.sh`, and gate evidence. |

## Merge policy

- Prefer the newest release-gate implementation for files changed between versions; do not overwrite newer code with older files.
- Preserve all supplied original ZIPs and their SHA-256 hashes so older snapshots remain recoverable and comparisons are reproducible.
- Keep optional clipboard-MCP and server-protection utilities outside the compiled/install target. They are not required to run the audio application and are not treated as trusted runtime dependencies.
- Keep PortAudio opt-in and system-provided (`HUSH_ENABLE_PORTAUDIO=ON`); the build does not fetch dependencies or access the network to build.
- Keep the release gate's warning that automated tests are not equivalent to hardware, UI acceptance, or security sign-off.

## Build and test performed for this reconciled package

Environment: Linux x86-64, CMake 3.31.6, GNU C++ 14.2.0. Configuration used Release mode, `HUSH_WARNINGS_AS_ERRORS=ON`, system PortAudio enabled when found, and optional X11 GUI enabled when found.

- CMake configure: PASS.
- Build of `hush`, `hush_gui`, audio/realtime/module libraries and test executables: PASS.
- Fast CTest suite: **6/6 PASS** (`hush_audio_tests`, `hush_realtime_tests`, `hush_module_tests`, `hush_lanes_tests`, `hush_msv_tests`, `hush_cli_smoke`).
- CLI help smoke: PASS; includes clip/session commands.
- Source checksum manifest: validated after excluding self-mutating generated release-gate logs from the manifest.
- Detector calibration is intentionally excluded from the fast gate and has not been rerun as part of this reconciliation.

## Known gaps — not a production DAW release

- No physical audio output/input device test was possible; device chooser/hot-plug recovery is not implemented.
- Recording is an early capture slice; input monitoring, punch-in/out, pre/post-roll and loop-take playlists are not complete.
- Autosave/crash recovery and complete GUI clip-editing/zoom/scroll/loop integration remain incomplete.
- MIDI is an event queue, not full OS MIDI I/O. Plugin support is an in-process interface, not VST3/CLAP/AU binary hosting.
- The X11 GUI build is verified, but only automated startup/render smoke has been documented; no human usability sign-off is claimed.
- No Windows/macOS/ARM native build, full sanitizer suite, TSan, parser fuzzing, independent security audit, or security certification is claimed.

**Release decision: automated gate PASS on this Linux host; production release remains HOLD.** See `RELEASE_GATE.md`, `PORTING_STATUS.md`, and `SECURITY_AND_PRIVACY.md`.

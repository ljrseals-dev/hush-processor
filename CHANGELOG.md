# Changelog

## HUSH 2.2.0 — 2026-10-14 (early development build)

- Added a visible Record / Stop Rec transport control to the Linux/X11 GUI.
- Added opt-in PortAudio duplex input/output capture, bounded preallocated capture memory, unique take filenames, and PCM16 WAV writing after the stream stops.
- Recording callback performs no allocation and no disk I/O; capture duration is bounded to five minutes by the GUI.
- Important limitations: no hardware validation in this environment; no track arm, punch, pre/post-roll, loop playlists/comping, live input monitoring through the channel strip, device chooser/hot-plug recovery, or autosave/crash recovery. Do not treat this as production-ready recording.

## HUSH 2.1.0 — 2026-10-13 (development build)

- Added `FKSESSION 2` clip metadata for source trim, finite length and fade-in/fade-out; reader remains compatible with `FKSESSION 1`.
- Added tested move, trim, split and fade APIs, including undo/redo integration tests and offline rendering of trims/fades.
- Added bounded/preloaded real-time mixer callback, transport, seek, track/master gain and mute, plus output peak metering.
- Added optional system PortAudio output backend (no download) and native Linux/X11 GUI with transport, arrangement, waveform overview, mixer, meter, save, undo and redo.
- Added CLI commands for session clip move/trim/split/fades; extended smoke tests to cover persistence. Added bounded SPSC MIDI event queue and an in-process DSP plugin-chain contract. These are interfaces/core support, not OS MIDI I/O or VST3/CLAP/AU binary hosting.
- Hardened WAV output with temporary-file + atomic create/replace behavior and explicit no-overwrite/symlink refusal; session saves use the same POSIX atomic strategy. Session load is size-bounded and rejects symlink inputs.
- Fresh Linux GCC 14.2 Release build and Clang 17 Release build passed with warnings as errors; CTest passed 7/7 on both. GUI survived a 3-second Xvfb launch smoke test.
- Physical audio-device playback was not tested; Windows/macOS/ARM builds, real MIDI I/O, third-party plugin formats, recording, full parser fuzzing and independent security certification remain open.


## HUSH 2.0.0 — 2026-10-12

- Renamed the product and command-line executable from Hushroom to HUSH.
- Updated CMake project/target names, build scripts, CLI help, active documentation, and release metadata.
- Adopted `2.0.0` for the product rename and command-name change; no `hushroom` compatibility executable is installed.
- Preserved the existing `FKSESSION 1` session-file format and `.fks` extension so existing sessions remain compatible at the format level.
- Added `FILE_STRUCTURE.md` and a staged completion roadmap.
- Changed the historical `scripts/build_gpp.sh` entry point to use the offline CMake build, avoiding repeated monolithic compilation of the same sources.
- Ran CMake configure/build with warnings as errors; CTest passed 6/6; the CLI smoke suite passed 55/55; ELF hardening inspection refreshed. Selected ASan/UBSan suites passed 4/4; the full sanitizer suite did not complete within the execution window.
- Corrected signed PCM24 decoding, made session percent-escape decoding strict, enforced the session clip-count limit, rejected symlink session-save targets, and prevented watermark output when its label sidecar already exists (unless overwrite is explicit).
- Explicitly recorded that HUSH remains an offline DAW core, not a complete graphical real-time DAW. No GUI, live audio engine/device backend, MIDI, or plugin hosting was added in this release.
- ASan/UBSan and TSan results from Hushroom 1.1.1 were moved under `evidence/legacy_hushroom_1.1.1/` and are not presented as fresh HUSH 2.0.0 sanitizer runs.

## 2.2.0 — Release gate hardening

- Added `scripts/release_gate.sh` for reproducible Release configure/build, fast CTest, CLI checks, and source-manifest verification.
- Added `RELEASE_GATE.md` documenting manual hardware gates and the distinction between automated pass and production approval.
- Fast gate passed on Linux x86_64 with GCC 14.2.0, warnings-as-errors, PortAudio system library enabled, and X11 GUI target built. Six tests passed; long-running detector calibration is a separate required sign-off.
- Physical audio-device testing, full recording workflow, input monitoring, punch/loop takes, and autosave/recovery remain unverified or unimplemented; production release remains on HOLD.

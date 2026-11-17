# Hush 2.0.0 — Technical Manual

## 1. Product scope

Hush is an offline-first C++20 audio/session core with a command-line interface. It is not yet a complete graphical, real-time digital audio workstation. This release does not contain a JUCE application, device I/O backend, timeline GUI, MIDI engine, or VST3/CLAP/AU hosting. Those are not represented as implemented.

## 2. Privacy and execution model

- The default application path performs local work only; no telemetry, analytics, remote control, auto-update, or undisclosed network service is part of the Hush executable.
- There are no automatic background tasks. A command runs only when the user invokes it.
- File operations require explicit paths. Existing output files are not overwritten unless `--overwrite` is supplied, and the application rejects input/output aliasing.
- Watermark embedding requires `--confirm` and writes a separate output file plus a label file; the input is not modified in place.
- Clipboard checking is off by default. This package has checker logic but no operating-system clipboard backend. `sensitive-check FILE` scans a file the user explicitly names.
- `optional/clipboard-mcp` and `optional/server-protection` are excluded from the CMake build and install. They are not trusted components of the Hush executable; read `optional/SECURITY_HOLD.md` before using them.

## 3. Build

Requirements: CMake 3.16+ and a C++20 compiler. The verified environment was Linux x86_64 with GCC 14.2.0.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHUSH_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The CMake build is offline: it does not fetch source, initialize Git submodules, or install dependencies. A prebuilt Linux x86_64 executable is included in `prebuilt-linux-x86_64/`; do not use that binary on other operating systems or CPU architectures.

## 4. Main modules

- `src/audio.*`: WAV reader (PCM 8/16/24/32-bit and IEEE float32), PCM16 WAV writer, tone generation, gain, mix and basic signal statistics.
- `src/time.*`: tempo/meter conversion, bars-beats-ticks, sample/second/beat conversion and SMPTE timecode including 29.97 drop-frame.
- `src/session.*`: offline session data model, tracks/clips, serialization, undo/redo support and WAV render.
- `src/automation.*` and `src/recorder.*`: breakpoint lanes, lane editing and a tested Write/Touch/Latch/Touch-Latch state machine. There is no live audio callback feeding this recorder yet.
- `src/markers.*` and `src/counter.*`: markers/selections/memory locations and formatted counter output; the counter is CLI output, not a GUI window.
- `src/forensics.*`: local file hash and narrowband indicators plus the hardened DawChecker-derived detectors. These are not validated forensic conclusions.
- `src/watermark.*`: experimental spread-spectrum watermark detector/embedder. Embedding is explicit and writes a new file.
- `src/clipboard.*`: consent-gated checker logic; no OS clipboard provider is implemented.
- `third_party/dawchecker_hardened/`: the fork built by Hush. `third_party/dawchecker/` is retained as an untouched reference and is not built.

## 5. CLI overview

Run `hush help` for the full current command list. The main command families are:

- Audio: `tone`, `info`, `scan`, `gain`, `mix`
- Forensics: `forensics`, `codec-scan`, `selfcheck`
- Watermark: `wm-detect`, `wm-embed`
- Sessions: `session-new`, `session-info`, `session-add-track`, `session-add-clip`, `session-gain`, `session-render`
- Automation: `lane-mode`, `lane-point`, `lane-range`, `lane-thin`, `lane-show`, `lane-record`
- Markers and counter: `marker-add`, `marker-list`, `marker-remove`, `marker-recall`, `marker-next`, `marker-prev`, `counter`
- Time and privacy utilities: `bbt`, `timecode`, `sensitive-check`

Example:

```sh
hush tone tone.wav 1.0 440 0.4 48000 2
hush info tone.wav
hush session-new song.fks 48000 120
hush session-add-track song.fks Synth
hush session-add-clip song.fks 0 tone.wav 0
hush session-render song.fks mix.wav
```

## 6. Compatibility and naming

Version 2.0.0 marks the product rename from Hushroom to Hush. The executable and CMake target names use `hush`; old `hushroom` command names are not shipped as aliases. The internal `fk` C++ namespace and `FKSESSION 1` session-file signature are intentionally retained to avoid an unnecessary internal/session-format migration. This package does not promise a stable public C++ ABI.

## 7. Test evidence and known limits

`BUILD_TEST_RESULTS.txt` records the fresh CMake build and test results. `evidence/ctest.txt`, `evidence/cli_smoke.txt` and `evidence/hardening_readelf.txt` are the corresponding current-release evidence. Prior Hushroom 1.1.1 evidence is archived in `evidence-hushroom-1.1.1/`.

All detector calibration is synthetic. The project has not been validated against real-world tampering cases or real recordings. Watermark audibility/robustness is not fully characterized. This release is not a security certification and does not claim to be a complete DAW.

## 8. Next engineering milestones for a full DAW

A real full DAW still needs an approved GUI/audio framework integration (the user requested JUCE, but no JUCE source or SDK was present in the supplied archive), real-time audio-device I/O, transport/clock, timeline editing and fades, live automation wiring, track/mixer GUI, MIDI, plugin hosting, additional media formats, cross-platform builds and DAW-host/listening tests. These are explicit future milestones, not hidden placeholders in this core release.

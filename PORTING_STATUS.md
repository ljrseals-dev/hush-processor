# Feature status — HUSH 2.2.0

Legend: **Implemented/tested** means source exists and relevant tests pass. **Partial** means a useful subset exists. **Not implemented** means no functional support is shipped.

| Area | Status | Verified scope / gap |
|---|---|---|
| C++20 offline build | Implemented/tested | GCC 14.2 and Clang 17 on Linux x86-64, warnings as errors |
| WAV reader/writer | Implemented/tested | PCM 8/16/24/32 and float32 input; atomic PCM16 output; path/symlink/no-overwrite tests |
| Session serialization | Implemented/tested | Reads FKSESSION 1; writes FKSESSION 2 with source trim, length, fades; bounded input size; symlink inputs rejected |
| Session editing API | Implemented/tested | Move, trim, split, fade, snapshot undo/redo; tests cover edit/undo/redo and render output |
| GUI undo/redo | Partial | Ctrl+Z/Ctrl+Y covers GUI gain/mute edits; clip editing remains library-only |
| Offline renderer | Implemented/tested | Track gain/mute, clip trim/fades, volume/mute automation; other automation types stored but not rendered |
| Real-time mixer core | Implemented/tested | Preloaded clips, callback path, transport, gain/mute, peak meter; no callback allocations by design |
| PortAudio output | Partial | Optional system library backend, default output stream; not physically tested with a real device here; no device chooser/hotplug handling |
| Linux GUI | Partial | X11 UI with transport, arrangement rows, waveform overview, mixer, mute/gain, peak meter, save/undo/redo; Xvfb smoke test passed |
| Windows/macOS GUI | Not implemented | No native frontend or platform build verified |
| MIDI | Partial | Event validation and bounded SPSC queue only; no OS MIDI backend or MIDI editing UI |
| Plugin host | Partial | In-process DSP interface and chain tested; no VST3/CLAP/AU loading, sandbox or plugin GUI |
| Audio recording/monitoring | Not implemented | Existing recorder is an event/state model, not a device capture path |
| DawChecker-derived forensics | Implemented/tested | Local indicators and synthetic calibration; real-world calibration and independent audit remain open |
| Watermark | Experimental | Explicit embed/detect and tests; audibility and robustness against codecs/resampling/attacks not established |
| OS clipboard integration | Not implemented | Consent-gated library logic only; no platform clipboard backend |
| Optional admin utilities | Excluded | Not built, installed, linked, or activated by HUSH |
| Release security certification | Not claimed | No independent audit, full fuzzing, or verified network-isolated release run |

## Remaining release work

1. Add clip selection/drag/split/trim/fade controls to the GUI and make all edit commands undoable.
2. Add device selection, robust stream lifecycle/hotplug recovery, latency tests, recording and monitoring.
3. Add OS MIDI I/O and MIDI track/editor support.
4. Choose a plugin standard/SDK and licensing policy before adding VST3/CLAP/AU binary hosting; implement sandboxing before loading third-party binaries.
5. Port GUI/device backends to Windows and macOS and build on native CI/toolchains.
6. Run WAV/session parser fuzzing, full sanitizer suites, real-device playback tests, network isolation tests and independent third-party review.
7. Do not call HUSH complete until these remaining items are implemented and verified.


## 2.2.0 recording status

- **Implemented in source:** GUI Record/Stop Rec control; default-device duplex PortAudio capture; bounded 5-minute float capture buffer; PCM16 WAV take write on stop; unique take filenames; no disk I/O in audio callback.
- **Built and tested here:** Linux GCC Release with warnings-as-errors; PortAudio system library found; X11 GUI target built; CTest 7/7 passed.
- **Not verified:** physical input/output hardware, real microphone/interface capture, latency/glitch performance, and recovery after process/OS crash.
- **Still missing:** track arming/routing, punch-in/out, pre/post-roll, loop playlist per take and comping, input monitoring through the full channel strip, autosave/recovery, device chooser/hot-plug, and automated recording hardware tests.

# HUSH security and privacy notes (2.1.0)

This document records scoped checks, not a security certification.

## Default build policy
- The active C++ application is designed for offline use. The build defines no network client, telemetry, crash upload, updater, remote control, startup persistence, or background service.
- It does not invoke optional installers or fetch dependencies. CMake has no FetchContent or external project download step.
- Reads and writes are command-driven. Output paths are checked; existing outputs require `--overwrite`; operations refuse input/output aliasing where applicable. Reports and watermark labels are only written when requested.
- Watermark embedding is experimental and requires `--confirm`; the input is not modified and the key is not written to the label.
- Clipboard checking is a consent-gated library with no OS clipboard backend in this build. The command `sensitive-check FILE` checks a named text file and does not print matched secret contents.
- `codec-scan` is explicit. The optional PortAudio output backend is system-library-only, no-download, and uses the default output device; physical playback has not been validated here.

## Tests and binary checks for HUSH 2.1.0
- Linux GCC and Clang configure/build passed with warnings treated as errors; CTest passed 7/7 suites on both toolchains. `scripts/run_all_tests.sh` normal stage passed module (98/98 checks), audio, MSV, lanes, calibration and 55-step CLI smoke tests.
- `evidence/hardening_readelf.txt` reports PIE, RELRO, BIND_NOW, NX stack, stack protector and FORTIFY symbols on inspected binaries. CET IBT/SHSTK was not detected and is not claimed.
- `evidence/SHA256SUMS_binaries.txt` records hashes for the binaries from this build.
- The CLI smoke suite passed outside the network namespace. Running the entire smoke shell under `unshare -rn` stalled after the first command in this environment; therefore isolation of that suite is unverified. Several individual normal-stage test binaries passed under `unshare -rn`.
- A fresh ASan/UBSan build passed the selected audio, lanes, MSV and CLI smoke suites (4/4; `evidence/asan_ubsan_selected.txt`). The full sanitizer CTest run did not finish within the execution window; module and calibration suites are not claimed as sanitizer-passing. Historical Hushroom 1.1.1 ASan/UBSan and TSan logs are segregated under `evidence/legacy_hushroom_1.1.1/`.

## Third-party and optional code
- `third_party/dawchecker_hardened/` is the fork built by HUSH; its provenance and patch are documented beside the code. `third_party/dawchecker/` is the original reference copy and is not built.
- `optional/clipboard-mcp/` and `optional/server-protection/` are unmodified optional tools. They are not linked, built, installed or activated by HUSH. Their scripts may read/write the OS clipboard, install packages, use network downloads, alter firewall configuration, create scheduled tasks or install services. Do not run them as part of HUSH without a separate review and explicit decision.
- The original DawChecker and optional-tool trees retain security-hold notes. Do not interpret their inclusion as endorsement or proof of safety.

## Remaining security work
- No real-network packet capture/DNS monitoring, comprehensive line-by-line third-party audit, parser fuzzing, cross-platform audit, side-channel/timing review, or independent security review has been completed for HUSH 2.1.0.
- The forensic detectors have only been calibrated on a synthetic corpus. Real recordings, codecs, room hum, and adversarial inputs have not been adequately validated.
- Watermark embedding audibility and robustness against resampling, lossy codecs, time stretching and deliberate attacks remain unverified.
- No security certification is claimed. A detector result is an indicator, not proof of tampering or authorship.


## 2.1.0 additions and remaining gaps
- WAV export, session save, forensic reports and watermark labels use same-directory temporary files and atomic create/replace on POSIX; no-overwrite uses an atomic hard-link create. Symbolic-link targets are refused. Session input is capped at 4 MiB and symlink session inputs are refused.
- The optional PortAudio backend is disabled by default. If explicitly enabled, it links only a system library discovered by CMake; no dependency is downloaded. Its system audio stack may expose configured remote-audio endpoints. It uses the OS default output device. No physical device playback test was available in this build environment.
- The GUI is Linux/X11 only and was launch-smoke-tested under Xvfb. Windows/macOS builds and native device backends are not claimed.
- MIDI and plugin support is limited to an in-memory event queue and an in-process DSP interface. No untrusted binary plugins are loaded.
- The source-pattern audit is advisory. It is not a complete line-by-line third-party audit, fuzzing campaign, or proof of no network activity.

## 2.2.0 early recording addition

The optional PortAudio recording path opens the operating system's default audio input and output when the user activates Record. PortAudio is opt-in at build time (`HUSH_ENABLE_PORTAUDIO=ON`); no audio is sent to a network by HUSH. Recording uses a bounded preallocated in-memory buffer and writes a PCM16 WAV after stopping. It has not been validated on physical hardware in this environment. Device permissions and system audio routing remain under the host OS and audio stack; do not enable PortAudio in environments where the configured devices/endpoints are not trusted.

This release does not provide crash-safe recording recovery, autosave, input monitoring, punch-in/out, pre/post-roll, or loop-take playlists. A process crash before stopping can lose the in-memory take. Do not use this experimental capture feature for irreplaceable sessions.

# HUSH 2.2.0 release gate

## Purpose

The release gate prevents a successful compile from being mistaken for a production-ready DAW. It separates repeatable automated checks from checks that require physical audio hardware or features that are not yet implemented.

## Run automated gate

```bash
scripts/release_gate.sh
```

An optional build directory can be supplied:

```bash
scripts/release_gate.sh /tmp/hush-release-build
```

The script configures a Release build with warnings as errors, opts into the system PortAudio backend if available (it never downloads it), builds all available targets, runs the fast CTest suite (excluding long-running detector calibration), checks CLI help, and verifies the source checksum manifest when present. Logs are written to `evidence/release_gate/`.

## Release policy

- **Automated PASS** means only the checks listed above passed on that host.
- **Early Access** is allowed with documented limitations and reproducible evidence.
- **Production release is HOLD** until applicable manual gates are signed off and the P1 requirements below are implemented and tested.

## Required sign-offs

| Gate | Evidence required | Current status |
|---|---|---|
| Build + fast automated tests | `evidence/release_gate/run.log` and test summary | Run locally per target toolchain; detector calibration is separate |
| Physical output | Device/model, OS, sample rate, buffer, duration, result | Not verified here |
| Physical input/recording | Captured WAV metadata, audible/loopback inspection, error-path tests | Not verified here |
| Device selection/hot-plug | Device switch/disconnect/reconnect results | Not implemented |
| Track arm/punch/pre/post-roll | Boundary-focused tests and GUI evidence | Not implemented |
| Loop recording/take playlist | Multiple takes retained and selectable | Not implemented |
| Input monitoring through channel strip | Routing/latency/feedback tests | Not implemented |
| Autosave/recovery | Forced-termination recovery test and no-loss evidence | Not implemented |
| Clip editing/zoom/scroll/loop | GUI integration tests and manual sign-off | Partial / not complete |
| Solo/pan/GUI bounce | Mix comparison and export verification | Not implemented |
| MIDI/plugin decision | Explicit support, license, and isolation decision | Pending |
| Security/release review | Dependency/license audit, parser fuzzing, platform review | Open |

## Required release evidence

For each supported OS/toolchain, attach the exact commit/source ZIP checksum, build log, test report, package checksum, and the operator/device details for hardware tests. Do not state “recording verified” without a physical or deterministic loopback capture test. Do not state “recovery verified” without intentionally terminating a test session and restoring it.


## Separate long-running calibration

The fast gate excludes `hush_detector_calibration` because it can exceed a short CI execution window. Run it separately with `ctest --test-dir <build-dir> --output-on-failure -R hush_detector_calibration` and archive the result before signing off the detector-calibration gate. A fast-gate pass alone is not a full calibration pass.

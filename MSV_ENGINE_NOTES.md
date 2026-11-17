# MSV engine fork: calibration notes (HUSH 2.2.0)
All numbers below come from committed outputs in `evidence/`. Everything is synthetic; no real recordings were tested.

## What changed in the detectors, and why
The upstream detectors summed unwindowed Goertzel energy forever and compared it to fixed constants, so any loud signal tripped them (measured on a plain 440 Hz tone and white noise). The fork instead:
1. Measures amplitude in **dBFS** with a Hann-windowed Goertzel bank over fixed blocks (a full-scale sine reads about 0 dBFS; checked in module tests: 0.01 amplitude reads -40 dBFS).
2. Decides by **margin over neighbouring frequency bins** (a tone must stand out from the spectrum on both sides), never by a raw energy constant.
3. Requires a **sustained run of blocks** (hysteresis) before Active/Alert, and steps down only after several clean blocks.
4. Derives every smoothing coefficient from a **time constant and the sample rate** (`alpha = 1 - exp(-blockSeconds/tau)`), and defines block lengths in seconds, so behaviour is the same at 44.1/48/96 kHz.

## Thresholds chosen (sensitivity 5; sensitivity 1..100 shifts the margins)
| Detector | Measurement | Hit rule | Sustain | Time constant | Max verdict |
|---|---|---|---|---|---|
| Ultrasonic | 85 ms Hann blocks; bands 17.4/18.0/18.5/19.2/20.15 kHz vs bins at +/-400 and +/-800 Hz | amp > -90 dBFS and margin >= 20 dB | Active after 3 blocks (about 0.26 s), Alert after 12 (about 1 s); 3 clean blocks step down one level | 250 ms | Alert |
| ENF | 2 s Hann windows; 50 and 60 Hz fundamentals scanned +/-1 Hz in 0.05 Hz steps vs bins at +/-4 and +/-5 Hz | peak > -90 dBFS and margin >= 15 dB | Active after 2 windows (4 s); 2 clean windows to clear | 2 s | **Active** (mains hum is common) |
| Reference residual | 50 ms blocks; residual (live - reference) level in dB re the reference | -60 dB <= level <= -20 dB and steady (std <= 6 dB over the last 8 blocks) | Active after 10 blocks (0.5 s) | 50 ms | **Active**; not watermark-specific |
Sensitivity mapping: ultrasonic margin = clamp(20 - 2(s-5), 10, 30) dB; ENF margin = clamp(15 - 1.5(s-5), 8, 25) dB; residual floor = clamp(-60 + 2(s-5), -80, -40) dB.

## Measured results (evidence/detector_calibration.txt, run under `unshare -rn` and again under ASan+UBSan)
- Ordinary corpus: 72 items = {silence, white noise, pink noise, speech-like AM noise, 440 Hz, 1 kHz} x {-40, -20, -6, 0 dBFS} x {44.1, 48, 96 kHz}, 10 s each. **Alert: 0. Active: 0.** on all four detector configurations (ultrasonic, ENF, residual with live==reference, residual with an unrelated reference).
- Controls (must be detected): 19.2 kHz sine 12/12 detected by the ultrasonic detector (Alert); 50 and 60 Hz sines 24/24 detected by ENF (Active).
- Observed false-positive rate: 0/72 per detector. With only 72 items the statistical upper bound is not tight (rule of three: about 4% at 95% confidence).
- Sample-rate invariance (msv_tests): identical verdicts at 44.1/48/96 kHz for a 19.2 kHz tone (Alert), a 60 Hz tone (Active) and white noise (Idle).
- Hysteresis (msv_tests): an 85 ms burst never trips a verdict at 9 different alignments; a 600 ms tone does. Documented limit: a burst of 2+ blocks (about 170 ms or more) can reach Active depending on alignment.

## Decisions I took at the gates (you did not answer them; I used the recommendations or the closest workable choice; please confirm or change)
1. Fork location: third_party/dawchecker_hardened/src/core/ (as recommended).
2. Tamper score: kept **internal**, not exposed in the CLI (no `--tamper-score`).
3. False-positive target: zero Alert on the ordinary corpus (met). **Deviation:** the master prompt lists 50/60 Hz and 19.2 kHz sines inside the ordinary corpus, but a detector that must find mains hum and ultrasonic tones cannot also stay silent on them. I treated those three as **controls** (must be detected) and everything else as ordinary.
4. Time constants: the proposed 10 ms (ENF) is meaningless for a detector that decides on 2 s windows, so the constants are 250 ms (ultrasonic, as proposed), 50 ms (residual, as proposed) and 2 s (ENF, window-matched). Please confirm.

## Residual limitations (not proven)
- No real recordings: speech, music, codecs, room hum, dither and clipping are untested. A sustained bass note near 50/60 Hz or a steady tone in 17-20 kHz (CRT whine, a test tone) WILL be reported; the detectors cannot tell a beacon from any other steady tone.
- ENF checks only the fundamentals (no harmonics, no frequency-track analysis). Verdict latency is at least 4 s.
- The residual detector reports a persistent low-level difference vs a reference; lossy coding, gain or dither cause that too. It is not evidence of a watermark.
- Seqlock fence ordering is not checked by GCC's ThreadSanitizer; 0 torn reads was observed on x86-64 only. Not tested on ARM.
- FTZ/DAZ is enabled for the thread that calls `prepare()` only; call `enableFlushDenormalsForThisThread()` on the audio thread if that differs. The tests check published values for subnormals, not CPU timing.
- Detections are indicators, never proof of tampering, authorship or authenticity.

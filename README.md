<div align="center">

# HUSH ENGINE v0.2.0
### *Zero Telemetry. Unlimited Nth Tracks. Deterministic Real-Time Audio.*

<img src="watermarked_img_686974314975895486.jpg" alt="Hush Engine v0.2.0 Banner" width="100%">

[![C++20 Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Zero Telemetry](https://img.shields.io/badge/Telemetry-None-success.svg)]()
[![SIMD Dispatch](https://img.shields.io/badge/SIMD-SSE2%20%7C%20AVX2%20%7C%20NEON-orange.svg)]()
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

</div>

---

## Overview

**Hush Engine** is a high-performance, privacy-hardened C++20 DAW core and audio processing framework engineered for deterministic real-time performance[span_0](start_span)[span_0](end_span). Built from the ground up with zero telemetry, zero placeholder stubs, and absolute hardware efficiency, Hush delivers jitter-free multitrack scaling for professional audio environments.

---

## Key Architectural Pillars

* **Zero Allocation on Audio Thread**: Pre-allocated track pooling and fixed memory models ensure absolute safety from garbage collection or heap allocation jitter during playback.
* **Lock-Free Concurrency**: Asynchronous communication between UI/worker threads and the real-time audio thread is governed by a high-performance **SPSC (Single-Producer Single-Consumer) command queue**, completely eliminating priority inversion and mutex contention[span_1](start_span)[span_1](end_span).
* **Runtime SIMD Dispatch**: Automatically detects and leverages hardware-native vector instruction sets (**SSE2**, **AVX2**, and **ARM NEON**) to accelerate heavy mix summing and DSP passes[span_2](start_span)[span_2](end_span).
* **Uncompromising DSP**: Features integrated Simper State Variable Filters (SVF), multi-stage peak limiters, and sample-level amplitude clamping (`std::clamp`) to completely eliminate digital clipping, sub-sample instability, and buffer underruns (xruns)[span_3](start_span)[span_3](end_span).

---

## Performance Benchmark Summary

Evaluated against standard low-latency tracking and high-density mixing workloads:

| Workload Profile | Sample Rate | Buffer Size | Active Tracks | Execution Time / Block | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Low-Latency Tracking** | 44.1 kHz | 128 samples | 64 tracks | 18.6 µs (AVX2) | Stable (0 Xruns)[span_4](start_span)[span_4](end_span) |
| **Heavy Mixing Stress** | 48.0 kHz | 512 samples | 128+ tracks | 42.1 µs (AVX2) | Stable (0 Xruns)[span_5](start_span)[span_5](end_span) |

---

## Project Structure

```text
hush/
├── include/hush/
│   ├── audio/         # Track, Mixer, Master Bus, and Engine APIs
│   ├── control/       # Lock-Free SPSC Command Queues
│   ├── core/          # RT Math, Denormal Protections, Types
│   ├── dsp/           # Simper SVF, Limiters, Smoothers, Subband Filters
│   └── simd/          # Vector Abstractions & Runtime Dispatch Headers
├── src/               # Implementation files for Audio, DSP, and SIMD kernels
├── benchmarks/        # Hush Bench mixing performance validation suites
├── tests/             # Unit and integration testing suites
└── tools/             # Hush Build Studio management utilities
```markdown
<div align="center">

# HUSH ENGINE v0.2.0
### *Zero Telemetry. Unlimited Nth Tracks. Deterministic Real-Time Audio.*

<img src="watermarked_img_686974314975895486.jpg" alt="Hush Engine v0.2.0 Banner" width="100%">

[![C++20 Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Zero Telemetry](https://img.shields.io/badge/Telemetry-None-success.svg)]()
[![SIMD Dispatch](https://img.shields.io/badge/SIMD-SSE2%20%7C%20AVX2%20%7C%20NEON-orange.svg)]()
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

</div>

---

## Overview

**Hush Engine** is a high-performance, privacy-hardened C++20 DAW core and audio processing framework engineered for deterministic real-time performance[span_0](start_span)[span_0](end_span). Built from the ground up with zero telemetry, zero placeholder stubs, and absolute hardware efficiency, Hush delivers jitter-free multitrack scaling for professional audio environments.

---

## Key Architectural Pillars

* **Zero Allocation on Audio Thread**: Pre-allocated track pooling and fixed memory models ensure absolute safety from garbage collection or heap allocation jitter during playback.
* **Lock-Free Concurrency**: Asynchronous communication between UI/worker threads and the real-time audio thread is governed by a high-performance **SPSC (Single-Producer Single-Consumer) command queue**, completely eliminating priority inversion and mutex contention[span_1](start_span)[span_1](end_span).
* **Runtime SIMD Dispatch**: Automatically detects and leverages hardware-native vector instruction sets (**SSE2**, **AVX2**, and **ARM NEON**) to accelerate heavy mix summing and DSP passes[span_2](start_span)[span_2](end_span).
* **Uncompromising DSP**: Features integrated Simper State Variable Filters (SVF), multi-stage peak limiters, and sample-level amplitude clamping (`std::clamp`) to completely eliminate digital clipping, sub-sample instability, and buffer underruns (xruns)[span_3](start_span)[span_3](end_span).

---

## Performance Benchmark Summary

Evaluated against standard low-latency tracking and high-density mixing workloads:

| Workload Profile | Sample Rate | Buffer Size | Active Tracks | Execution Time / Block | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Low-Latency Tracking** | 44.1 kHz | 128 samples | 64 tracks | 18.6 µs (AVX2) | Stable (0 Xruns)[span_4](start_span)[span_4](end_span) |
| **Heavy Mixing Stress** | 48.0 kHz | 512 samples | 128+ tracks | 42.1 µs (AVX2) | Stable (0 Xruns)[span_5](start_span)[span_5](end_span) |

---

## Project Structure

```text
hush/
├── include/hush/
│   ├── audio/         # Track, Mixer, Master Bus, and Engine APIs
│   ├── control/       # Lock-Free SPSC Command Queues
│   ├── core/          # RT Math, Denormal Protections, Types
│   ├── dsp/           # Simper SVF, Limiters, Smoothers, Subband Filters
│   └── simd/          # Vector Abstractions & Runtime Dispatch Headers
├── src/               # Implementation files for Audio, DSP, and SIMD kernels
├── benchmarks/        # Hush Bench mixing performance validation suites
├── tests/             # Unit and integration testing suites
└── tools/             # Hush Build Studio management utilities

```
## Quick Start & Compilation
Clone the repository and build the engine using **CMake** and a modern C++20 compiler:
```bash
# Clone repository
git clone [https://github.com/hush-audio/hush.git](https://github.com/hush-audio/hush.git)
cd hush

# Configure build with CMake (Release mode)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile binaries and benchmarks
cmake --build build --config Release

# Run the high-performance mixer benchmark
./build/benchmarks/hush_bench

```
## License
Distributed under the MIT License. See LICENSE for more information.
```

```

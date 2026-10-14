<div align="center">

# HUSH PROCESSOR v0.2.0
### *Zero Telemetry. Deterministic Real-Time Audio Engine.*

<img src="1792013329061.jpg" alt="Hush Engine v0.2.0 Banner" width="100%">

[![C++20 Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Zero Telemetry](https://img.shields.io/badge/Telemetry-None-success.svg)]()
[![SIMD Dispatch](https://img.shields.io/badge/SIMD-SSE2%20%7C%20AVX2%20%7C%20NEON-orange.svg)]()
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

</div>

---
# Hush Processor (`HUSH-2.2.0-reconciled`)

(https://github.com/ljrseals-dev/hush-processor/actions/workflows/ci.yml)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17%2F20-blue.svg)](https://isocpp.org/)
[![License](https://img.shields.io/badge/license-Proprietary%2FLimited-green.svg)](LICENSE)

A high-performance C++ audio processing and plugin-hosting engine engineered for real-time audio workflows, comprehensive session management, and robust hardware integration.

---

## About the Project

**Hush Processor** is a modular C++ application designed for advanced audio manipulation, real-time monitoring, and extensible plugin management. Built with a focus on low-latency performance, it combines a dedicated PortAudio backend, flexible automation modules, and an X11 graphical interface framework[span_0](start_span)[span_0](end_span).

---

## Key Features

* **Advanced Audio Engine**: Low-latency audio processing pipeline backed by PortAudio and modular core components[span_1](start_span)[span_1](end_span).
* **Plugin Hosting**: Dynamic `plugin_host` architecture designed to load, manage, and execute external audio processing plugins seamlessly[span_2](start_span)[span_2](end_span).
* **Comprehensive Core Utilities**: Includes built-in modules for automation, MIDI control, session management, watermarking, forensics, and clipboard integration[span_3](start_span)[span_3](end_span).
* **Graphical User Interface**: Lightweight X11 GUI support (`gui_x11`) for visual interaction and control[span_4](start_span)[span_4](end_span).
* **Rigorous Testing Suite**: Comprehensive unit and integration test coverage verifying audio pipelines, detectors, lanes, and real-time behavior[span_5](start_span)[span_5](end_span).

---

## Repository Structure

```text
├── .github/workflows/ci.yml     # Continuous Integration workflows[span_6](start_span)[span_6](end_span)
├── src/                         # Core C++ source files[span_7](start_span)[span_7](end_span)
│   ├── audio.cpp / .hpp         # Core audio processing logic[span_8](start_span)[span_8](end_span)
│   ├── audio_engine.cpp / .hpp  # Main audio engine orchestration[span_9](start_span)[span_9](end_span)
│   ├── portaudio_backend.cpp    # PortAudio hardware backend implementation[span_10](start_span)[span_10](end_span)
│   ├── plugin_host.cpp          # Plugin hosting and management system[span_11](start_span)[span_11](end_span)
│   ├── gui_x11.cpp              # X11 graphical user interface module[span_12](start_span)[span_12](end_span)
│   └── main.cpp                 # Application entry point[span_13](start_span)[span_13](end_span)
├── tests/                       # Test suite (audio, detectors, lanes, MSV)[span_14](start_span)[span_14](end_span)
├── CMakeLists.txt               # CMake build configuration[span_15](start_span)[span_15](end_span)
└── *.md                         # Project documentation and historical notes[span_16](start_span)[span_16](end_span)
Here is the updated, icon-free README.md with the fixed CI graphic badge and proper text spacing/formatting:
```markdown
# Hush Processor (`HUSH-2.2.0-reconciled`)

[![CI Status](https://github.com/ljrseals-dev/hush-processor/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/ljrseals-dev/hush-processor/actions/workflows/ci.yml)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17%2F20-blue.svg)](https://isocpp.org/)
[![License](https://img.shields.io/badge/license-Proprietary%2FLimited-green.svg)](LICENSE)

A high-performance C++ audio processing and plugin-hosting engine engineered for real-time audio workflows, comprehensive session management, and robust hardware integration.

---

## About the Project

**Hush Processor** is a modular C++ application designed for advanced audio manipulation, real-time monitoring, and extensible plugin management. Built with a focus on low-latency performance, it combines a dedicated PortAudio backend, flexible automation modules, and an X11 graphical interface framework[span_0](start_span)[span_0](end_span).

---

## Key Features

* **Advanced Audio Engine**: Low-latency audio processing pipeline backed by PortAudio and modular core components[span_1](start_span)[span_1](end_span).
* **Plugin Hosting**: Dynamic `plugin_host` architecture designed to load, manage, and execute external audio processing plugins seamlessly[span_2](start_span)[span_2](end_span).
* **Comprehensive Core Utilities**: Includes built-in modules for automation, MIDI control, session management, watermarking, forensics, and clipboard integration[span_3](start_span)[span_3](end_span).
* **Graphical User Interface**: Lightweight X11 GUI support (`gui_x11`) for visual interaction and control[span_4](start_span)[span_4](end_span).
* **Rigorous Testing Suite**: Comprehensive unit and integration test coverage verifying audio pipelines, detectors, lanes, and real-time behavior[span_5](start_span)[span_5](end_span).

---

## Repository Structure

```text
├── .github/workflows/ci.yml     # Continuous Integration workflows[span_6](start_span)[span_6](end_span)
├── src/                         # Core C++ source files[span_7](start_span)[span_7](end_span)
│   ├── audio.cpp / .hpp         # Core audio processing logic[span_8](start_span)[span_8](end_span)
│   ├── audio_engine.cpp / .hpp  # Main audio engine orchestration[span_9](start_span)[span_9](end_span)
│   ├── portaudio_backend.cpp    # PortAudio hardware backend implementation[span_10](start_span)[span_10](end_span)
│   ├── plugin_host.cpp          # Plugin hosting and management system[span_11](start_span)[span_11](end_span)
│   ├── gui_x11.cpp              # X11 graphical user interface module[span_12](start_span)[span_12](end_span)
│   └── main.cpp                 # Application entry point[span_13](start_span)[span_13](end_span)
├── tests/                       # Test suite (audio, detectors, lanes, MSV)[span_14](start_span)[span_14](end_span)
├── CMakeLists.txt               # CMake build configuration[span_15](start_span)[span_15](end_span)
└── *.md                         # Project documentation and historical notes[span_16](start_span)[span_16](end_span)

```
## Building & Installation
### Prerequisites
Ensure your system has the following dependencies installed before building:
 * A modern C++ compiler supporting C++17 or later (GCC, Clang, or MSVC)
 * CMake (version 3.15 or higher)
 * PortAudio development libraries (libportaudio2 / portaudio-dev)
 * X11 development headers (for graphical interface support)
### Quick Start
 1. **Clone the repository:**
   ```bash
   git clone [https://github.com/ljrseals-dev/hush-processor.git](https://github.com/ljrseals-dev/hush-processor.git)
   cd hush-processor
   
   ```
 2. **Configure the build with CMake:**
   ```bash
   mkdir build && cd build
   cmake ..
   
   ```
 3. **Compile the application:**
   ```bash
   cmake --build . --config Release
   
   ```
## Testing & Quality Assurance
The repository includes a robust testing suite located in the tests/ directory to validate stability, detector calibration, and real-time performance. Run the tests using CTest after building:
```bash
cd build
ctest --output-on-failure

```
## Documentation
Detailed technical notes, reconciliation history, and build results are available directly in the repository root:
 * MSV_ENGINE_NOTES.md
 * RECONCILIATION.md
 * SECURITY_AND_PRIVACY.md
 * LOCAL_BUILD_RESULTS.md
```

```

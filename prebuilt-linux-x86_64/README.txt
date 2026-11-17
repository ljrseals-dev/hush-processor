HUSH 2.2.0 — Linux x86-64 prebuilt binaries

Built from the included reconciled source on Linux x86-64 with GNU C++ 14.2.0 and CMake 3.31.6.
The CLI binary is `hush`; the optional X11 desktop executable is `hush_gui`.

Run `./hush help` to see supported commands. `hush_gui` requires an X11 display and the system X11 runtime libraries.
PortAudio was enabled at compile time using the system library, but no physical audio device was available for hardware validation. Do not interpret the binary as proof of hardware recording or monitoring support.

These binaries are platform-specific. Build from the source with `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHUSH_WARNINGS_AS_ERRORS=ON -DHUSH_ENABLE_PORTAUDIO=ON -DHUSH_BUILD_X11_GUI=ON` and `cmake --build build -j2` for your own host.

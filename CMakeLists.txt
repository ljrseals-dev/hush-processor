cmake_minimum_required(VERSION 3.16)
project(HUSH VERSION 2.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(HUSH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (HUSH code only)" OFF)
option(HUSH_ENABLE_PORTAUDIO "Opt in to the system PortAudio hardware output backend (no download)" OFF)

# The build is offline-first: no FetchContent, no git operations, dependency downloads, or install scripts.
# PortAudio and X11 are discovered only as optional system libraries; they are never downloaded.

# ---- Hardening flags (mirrors build_gpp.sh; GCC/Clang on x86-64 Linux) ----
set(HUSH_HARDEN OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  set(HUSH_HARDEN ON)
  add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=3 -fcf-protection=full -fPIE)
  # _FORTIFY_SOURCE needs optimisation; make sure Debug builds do not silently drop it
  string(APPEND CMAKE_CXX_FLAGS_DEBUG " -O1")
endif()
# -pie and the -z flags are for executables only (a global add_link_options would also hit shared libraries)
function(hr_harden_exe target)
  if(HUSH_HARDEN)
    target_link_options(${target} PRIVATE -pie -Wl,-z,relro,-z,now,-z,noexecstack)
  endif()
endfunction()

function(hr_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(HUSH_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(HUSH_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# DawChecker HARDENED FORK (third_party/dawchecker_hardened; MIT). The original third_party/dawchecker/ is kept
# byte-identical for reference and is NOT built.
set(DC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dawchecker_hardened/src/core)
add_library(dawchecker_core STATIC
  ${DC}/DetectorTypes.cpp ${DC}/ENFAnalyzer.cpp ${DC}/UltrasonicScanner.cpp ${DC}/WatermarkDetector.cpp
  ${DC}/MSV_Engine.cpp ${DC}/Neutralizers.cpp ${DC}/CodecScanner.cpp ${DC}/AudioDeviceManager.cpp
  ${DC}/SecurityHardening.cpp)
target_include_directories(dawchecker_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dawchecker_hardened/src)
target_include_directories(dawchecker_core PUBLIC ${DC})

add_library(hush_audio STATIC src/audio.cpp)
target_include_directories(hush_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
hr_warnings(hush_audio)

add_library(hush_realtime STATIC src/audio_engine.cpp src/midi.cpp src/plugin_host.cpp src/portaudio_backend.cpp)
target_link_libraries(hush_realtime PUBLIC hush_audio)
if(HUSH_ENABLE_PORTAUDIO)
  find_library(HUSH_PORTAUDIO_LIBRARY NAMES portaudio libportaudio.so.2)
  if(HUSH_PORTAUDIO_LIBRARY)
    target_compile_definitions(hush_realtime PRIVATE HUSH_HAS_PORTAUDIO=1)
    target_link_libraries(hush_realtime PRIVATE ${HUSH_PORTAUDIO_LIBRARY})
    message(STATUS "HUSH: enabling system PortAudio backend (${HUSH_PORTAUDIO_LIBRARY}); no network fetch")
  else()
    message(STATUS "HUSH: PortAudio not found; hardware backend disabled (offline core remains buildable)")
  endif()
endif()
target_include_directories(hush_realtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
hr_warnings(hush_realtime)

add_library(hush_modules STATIC
  src/hash.cpp src/time.cpp src/watermark.cpp src/clipboard.cpp src/forensics.cpp src/session.cpp
  src/automation.cpp src/recorder.cpp src/markers.cpp src/counter.cpp)
target_link_libraries(hush_modules PUBLIC hush_audio dawchecker_core)
hr_warnings(hush_modules)

add_executable(hush src/main.cpp)
target_link_libraries(hush PRIVATE hush_modules hush_realtime)
hr_warnings(hush)
hr_harden_exe(hush)

option(HUSH_BUILD_X11_GUI "Build the optional native X11 desktop GUI when X11 is installed" ON)
if(HUSH_BUILD_X11_GUI AND UNIX AND NOT APPLE)
  find_package(X11 QUIET)
  if(X11_FOUND)
    add_executable(hush_gui src/gui_x11.cpp)
    target_include_directories(hush_gui PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(hush_gui PRIVATE hush_modules hush_realtime ${X11_LIBRARIES})
    hr_warnings(hush_gui)
    hr_harden_exe(hush_gui)
    message(STATUS "HUSH: enabling optional native X11 GUI")
  else()
    message(STATUS "HUSH: X11 not found; desktop GUI target disabled")
  endif()
endif()

include(CTest)   # provides BUILD_TESTING (default ON)
if(BUILD_TESTING)
  find_package(Threads REQUIRED)   # msv_tests uses std::thread
  find_program(BASH_EXECUTABLE bash)
  add_executable(hush_tests tests/audio_tests.cpp)
  target_link_libraries(hush_tests PRIVATE hush_audio)
  hr_warnings(hush_tests)
  hr_harden_exe(hush_tests)
  add_test(NAME hush_audio_tests COMMAND hush_tests)

  add_executable(module_tests tests/module_tests.cpp)
  target_link_libraries(module_tests PRIVATE hush_modules hush_realtime)
  add_executable(realtime_tests tests/realtime_tests.cpp)
  target_link_libraries(realtime_tests PRIVATE hush_modules hush_realtime)
  hr_warnings(realtime_tests)
  hr_harden_exe(realtime_tests)
  add_test(NAME hush_realtime_tests COMMAND realtime_tests)
  hr_warnings(module_tests)
  hr_harden_exe(module_tests)
  add_test(NAME hush_module_tests COMMAND module_tests)

  add_executable(lanes_tests tests/lanes_tests.cpp)
  target_link_libraries(lanes_tests PRIVATE hush_modules)
  hr_warnings(lanes_tests)
  hr_harden_exe(lanes_tests)
  add_test(NAME hush_lanes_tests COMMAND lanes_tests)

  add_executable(msv_tests tests/msv_tests.cpp)
  target_link_libraries(msv_tests PRIVATE dawchecker_core Threads::Threads)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # test-only: GCC false positive when a replacement operator new is built on malloc/free (allocation counter)
    target_compile_options(msv_tests PRIVATE -Wno-mismatched-new-delete)
  endif()
  hr_harden_exe(msv_tests)
  add_test(NAME hush_msv_tests COMMAND msv_tests)

  add_executable(detector_calibration tests/detector_calibration.cpp)
  target_link_libraries(detector_calibration PRIVATE dawchecker_core)
  hr_warnings(detector_calibration)
  hr_harden_exe(detector_calibration)
  add_test(NAME hush_detector_calibration COMMAND detector_calibration)

  if(BASH_EXECUTABLE)
    add_test(NAME hush_cli_smoke COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/smoke_test.sh $<TARGET_FILE:hush>)
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS hush RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(TARGET hush_gui)
  install(TARGETS hush_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
# optional/ (clipboard-mcp, server-protection) is deliberately NOT referenced by this build or install.

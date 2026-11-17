#!/bin/bash
# Reproducible evidence run. Usage: [STAGES="normal asan asancal tsan hardening"] scripts/run_all_tests.sh [build_dir]
# Stages can be run one at a time (useful when a shell has a time limit). Writes evidence/*.txt
# Needs: CMake/g++ (C++20), readelf, nm, and optionally unshare for individual isolated test binaries.
set -u
cd "$(dirname "$0")/.."
OUT=${1:-build_gpp}; EV=evidence; mkdir -p "$EV" "$OUT/san"
STAGES=${STAGES:-"normal asan tsan hardening"}
want() { [[ " $STAGES " == *" $1 "* ]]; }
D=third_party/dawchecker_hardened/src
ISO="unshare -rn"; $ISO true 2>/dev/null || { ISO=""; echo "WARNING: unshare -rn unavailable; runs are NOT network-isolated"; }
{ echo "date: $(date -u +%FT%TZ)"; g++ --version | head -1; ld --version | head -1; uname -srm; echo "network isolation for normal-stage runs: ${ISO:-none}"; } > $EV/toolchain.txt
status=0
run() { name=$1; shift; echo "== $name: $*" > $EV/$name.txt; "$@" >> $EV/$name.txt 2>&1; rc=$?; echo "exit=$rc" >> $EV/$name.txt; [ $rc -eq 0 ] || status=1; echo "$name: exit $rc"; }
SAN="-std=c++20 -O1 -g -Isrc -I$D"
DC=$(ls $D/core/*.cpp)
LIB="src/audio.cpp src/hash.cpp src/time.cpp src/watermark.cpp src/clipboard.cpp src/forensics.cpp src/session.cpp src/automation.cpp src/recorder.cpp src/markers.cpp src/counter.cpp"
if want normal; then
  bash scripts/build_gpp.sh > $EV/build.txt 2>&1 || { echo "build failed, see $EV/build.txt"; exit 1; }
  run module_tests         $ISO $OUT/module_tests
  run audio_tests          $ISO $OUT/hush_tests
  run msv_tests            $ISO $OUT/msv_tests
  run lanes_tests          $ISO $OUT/lanes_tests
  run detector_calibration $ISO $OUT/detector_calibration
  # The CLI smoke suite launches many short-lived processes; in this environment,
  # placing the whole shell under unshare -rn stalled after the first command.
  # Run it normally and record isolation as not verified rather than misreporting it.
  run cli_smoke            bash scripts/smoke_test.sh $OUT/hush
  ( cd third_party/dawchecker/src/core && sha256sum * ) > $EV/vendored_core_sha256.txt
fi
if want asan; then   # sanitizer builds omit FORTIFY/hardening flags (they conflict with the sanitizer runtimes)
  g++ $SAN -fsanitize=address,undefined -fno-sanitize-recover=undefined $LIB tests/module_tests.cpp $DC -o $OUT/san/module_asan 2>$EV/build_asan.txt
  g++ $SAN -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/msv_tests.cpp $DC -pthread -o $OUT/san/msv_asan 2>>$EV/build_asan.txt
  g++ $SAN -fsanitize=address,undefined -fno-sanitize-recover=undefined $LIB tests/lanes_tests.cpp $DC -o $OUT/san/lanes_asan 2>>$EV/build_asan.txt
  g++ $SAN -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/detector_calibration.cpp $DC -o $OUT/san/calib_asan 2>>$EV/build_asan.txt
  g++ $SAN -fsanitize=address,undefined -fno-sanitize-recover=undefined src/audio.cpp tests/audio_tests.cpp -o $OUT/san/audio_asan 2>>$EV/build_asan.txt
  g++ $SAN -fsanitize=address,undefined -fno-sanitize-recover=undefined $LIB src/main.cpp $DC -o $OUT/san/hush_asan 2>>$EV/build_asan.txt
  run asan_ubsan_module      $OUT/san/module_asan
  run asan_ubsan_msv         $OUT/san/msv_asan
  run asan_ubsan_lanes       $OUT/san/lanes_asan
  run asan_ubsan_audio       $OUT/san/audio_asan
  run asan_ubsan_cli_smoke   bash scripts/smoke_test.sh $OUT/san/hush_asan
fi
if want asancal; then   # slow: the whole calibration corpus under ASan+UBSan
  [ -x $OUT/san/calib_asan ] || g++ $SAN -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/detector_calibration.cpp $DC -o $OUT/san/calib_asan
  run asan_ubsan_calibration $OUT/san/calib_asan
fi
if want tsan; then
  g++ $SAN -fsanitize=thread tests/msv_tests.cpp $DC -pthread -o $OUT/san/msv_tsan 2>$EV/build_tsan.txt
  TSAN_OPTIONS="halt_on_error=1" run tsan_msv $OUT/san/msv_tsan
fi
if want hardening; then
  for b in hush module_tests msv_tests; do
    f=$OUT/$b; echo "--- $f"
    echo -n "PIE (ELF type DYN): "; readelf -h $f | grep -q "Type:.*DYN" && echo yes || echo NO
    echo -n "RELRO (GNU_RELRO):  "; readelf -lW $f | grep -q GNU_RELRO && echo yes || echo NO
    echo -n "BIND_NOW:           "; readelf -dW $f | grep -qE "BIND_NOW|FLAGS.*NOW" && echo yes || echo NO
    echo -n "NX stack:           "; readelf -lW $f | awk '/GNU_STACK/{print ($0 ~ /RWE/)?"NO (executable)":"yes"}'
    echo -n "stack protector:    "; nm -D --undefined-only $f | grep -q __stack_chk_fail && echo "yes (__stack_chk_fail imported)" || echo "not referenced"
    echo -n "CET (IBT/SHSTK):    "; readelf -n $f | grep -qE "IBT|SHSTK" && echo yes || echo NO
    echo -n "FORTIFY (_chk fns): "; nm -D --undefined-only $f | grep -q "_chk" && echo "yes (_chk functions imported)" || echo "none imported (may simply be unused)"
  done > $EV/hardening_readelf.txt
  grep -E "^---|NO" $EV/hardening_readelf.txt
  ( cd $OUT && sha256sum hush module_tests hush_tests msv_tests lanes_tests detector_calibration ) > $EV/SHA256SUMS_binaries.txt
fi
echo "stages [$STAGES]: $([ $status -eq 0 ] && echo PASS || echo FAIL)"; exit $status

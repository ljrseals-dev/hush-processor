# Known issues in the vendored (unmodified) DawChecker source: verified, none are active in FortKnox
Only the six files listed in PROVENANCE.md are compiled; FortKnox never calls the items below.
- `src/core/MSV_Engine.h`: watermarkScrubEnabled, enfNotchEnabled, ultrasonicRolloffEnabled all default to **true** (automatic neutralization). Not compiled or called by FortKnox.
- `src/core/WatermarkDetector.*`: uses std::deque push_back/pop_front inside process() (heap allocation; not real-time safe). FortKnox only calls it offline.
- `src/gui/Application.cpp` (NOT vendored): calls startAudio() unconditionally at launch (line 265) and opens a releases URL.
- `Source/dawchecker_vst3.cpp` and `latest/` (NOT vendored): `CryptographicSealer::computeSHA256` is **not SHA-256** (a few lines of rotate/xor) although its comment says FIPS 180-4, and the "signature" is that hash XOR 0x5A. Anything sealed with it has no cryptographic value. `latest/` is the variant with the compile fixes (no invalid `setControllerClass`); `Source/` still has it. FortKnox uses its own SHA-256, checked against NIST vectors (tests).
- SecurityHardening::isSafeExportPath only rejects '..', empty and NUL. FortKnox adds symlink, directory and overwrite checks around it.

# SoundCraft attribution
src/automation.* and src/markers.* are a C++ TRANSLATION of the data-model logic in SoundCraft's `crates/model/src/automation.rs`
and `crates/model/src/markers.rs` (breakpoint lanes, automation modes, parameter ids, value interpolation, write_range, thin,
shift_from; memory locations). SoundCraft is (c) 2026 ArtCraft Team and the SoundCraft contributors, licensed MIT OR Apache-2.0
(license texts and NOTICE in this directory; the MIT option is relied on here). Origin: the user-supplied soundcraft-main.zip.
NOT translated (not present in that zip): live Write/Touch/Latch capture. src/recorder.* is original code written for Hushroom;
its mode rules are documented in LANES_AND_MARKERS_NOTES.md. The soundcraft-automation crate (MCP server / control channel)
was deliberately NOT ported: the master spec excludes remote-control mechanisms.

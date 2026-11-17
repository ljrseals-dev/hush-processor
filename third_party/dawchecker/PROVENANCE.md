# DawChecker core (vendored, unmodified)
- Origin: user-supplied archive DawChecker-Secure-Ultimate-with-ServerProtection-1.zip (SHA-256 9dc185fe4238f93119c48ff9e30985b1a468a9df737e4ce2ab31f9067f7f3735), path `src/core/`.
- License: MIT, Copyright (c) 2026 Lewis James Seals Jr (see LICENSE). Files are byte-identical to the archive.
- Used: ENFAnalyzer, UltrasonicScanner, WatermarkDetector, CodecScanner, AudioDeviceManager, SecurityHardening, DetectorTypes.
- Present but NOT called by FortKnox: Neutralizers (WatermarkScrubber removes detected watermark residue) and MSV_Engine's process() path, which applies them.
- Built without PortAudio/ALSA: AudioDeviceManager enumerates nothing in this configuration.

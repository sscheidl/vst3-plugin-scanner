# Project status

Date: 2026-09-12
Version: 2.4.0
Branch: `restart/vst3-sdk-probe`

## Active scanner

The active codebase is a native Windows x64 VST3 scanner consisting of:

- `Vst3ProbeGui.exe`: Win32 interface, recursive folder scan, sorting, and export;
- `Vst3MetadataProbe.exe`: isolated process for exactly one VST3 module;
- the official Steinberg VST3 SDK as a pinned Git submodule;
- a strictly validated JSON protocol between the GUI and the probe.

The probe queries factory and ClassInfo metadata only. It does not create a
plug-in instance or modify plug-in files. Version, vendor, name, CID, categories,
and SDK version come directly from the VST3 factory. No version or vendor
heuristics are used.

The parser validates the complete schema-2 structure, including agreement
between `classCount` and the class array, and between `version` and
`versionMissing`. The table and both export formats show whether the factory
reported a version. Repeated audio CIDs from the same module are not counted
more than once.

## Safety and behavior

- Every module runs in its own Windows Job Object.
- Initial timeout: 15 seconds; exactly one retry with a 30-second limit.
- Stop, timeout, and window close terminate the entire probe process tree.
- Only the three standard handles are inherited by the probe process.
- Crashes and inconsistent process responses have dedicated diagnostics.
- A failed process termination cannot block the GUI indefinitely.
- `.vst3` files and bundle directories are discovered recursively.
- Directory symlinks are not followed.

## Cache and export

The optional cache is disabled by default. When enabled, the only cache file is
`vst3_scanner_cache.json` next to the GUI executable. A cache hit requires an
unchanged path, file sizes, and modification times. The content of VST3/DLL
binaries and `moduleinfo.json` is hashed as well. Only successful, revalidated
probe responses are stored.

The GUI table and CSV use the same column order. Multi-plug-in modules such as
WaveShells appear as one row per audio class with a shared module file. CID
remains available internally for duplicate detection and in JSON, but is hidden
from the GUI and CSV. CSV uses a UTF-8 BOM, semicolon separators, and neutralizes
formula-like values from untrusted metadata.

The GUI manifest enables Common Controls v6, Per-Monitor V2 DPI awareness, and
long-path support. Controls use the DPI-aware system message font where available.

## Historical codebase

The previous passive C++ scanner is preserved under the
`cpp-v1.1.0-pre-restart` tag. Its heuristics and old project files are not part
of the active branch.

## Real-world reference tests for 2.4.0

- WaveShell 17.1: four audio classes with four reported version values;
- WaveShell 12.7, 16.0, and 16.7: factory loads but exports zero classes;
- Guitar Rig 7: valid audio class after approximately 13.6 seconds;
- Komplete Kontrol: valid audio class after approximately 15 seconds, making it
  an expected candidate for the single 30-second retry.

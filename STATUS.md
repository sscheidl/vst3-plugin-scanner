# Plugin Inventory Scanner project status

Date: 2026-09-25
Version: 2.5.0
Branch: `restart/vst3-sdk-probe`

## Release status

The defined VST 3 inventory scope is feature-complete and ready for final
external code review. Version 2.5.0 is a standalone Windows x64 release; no
online version service or plug-in management component is planned.

## Active scanner

The active codebase is a native Windows x64 VST 3 scanner consisting of:

- `PluginInventoryScanner.exe`: Win32 interface, recursive folder scan, sorting,
  and export;
- `Vst3MetadataProbe.exe`: isolated process for exactly one VST3 module;
- the official Steinberg VST 3 SDK as a pinned Git submodule;
- a strictly validated JSON protocol between the GUI and the probe.

The probe queries factory and ClassInfo metadata only. It does not create a
plug-in instance or modify plug-in files. Version, vendor, name, CID, categories,
and SDK version come directly from the VST3 factory. No version or vendor
heuristics are used.

The parser validates the complete schema-2 structure, including agreement
between `classCount` and the class array, and between `version` and
`versionMissing`. The table and all export formats show whether the factory
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

The standalone HTML report escapes all third-party metadata, summarizes the
scan, marks issue and duplicate rows, and supports local filtering and sortable
columns without external assets or network access.

The GUI manifest enables Common Controls v6, Per-Monitor V2 DPI awareness, and
long-path support. Controls use the DPI-aware system message font where available.

## Historical codebase

The previous passive C++ scanner is preserved under the
`cpp-v1.1.0-pre-restart` tag. Its heuristics and old project files are not part
of the active branch.

## Real-world reference tests for 2.5.0

- WaveShell 17.1: four audio classes with four reported version values;
- WaveShell 12.7, 16.0, and 16.7: factory loads but exports zero classes;
- Guitar Rig 7: valid audio class after approximately 13.6 seconds;
- Komplete Kontrol: valid audio class within the 15-second initial attempt in a
  regular GUI scan; one isolated cold probe run stalled and was terminated,
  confirming the need for process isolation and bounded retries.

## Final validation

- Release x64 build completes with zero compiler errors.
- `Vst3ProbeTests` and `Vst3InventoryTests` pass.
- A practical GUI smoke test scanned 65 real modules before a controlled stop,
  retained the completed rows, sorted the table, and exported matching HTML,
  CSV, and JSON inventories.
- The HTML report was generated as a standalone document, the CSV contained a
  UTF-8 BOM and semicolon delimiters, and the JSON parsed successfully.
- Closing the GUI left no scanner or probe process running.

## Actual limitations

- Metadata quality is limited to values exposed by each VST3 factory. A missing
  or nonstandard factory version remains explicitly marked as not reported.
- Third-party modules can be slow, hang, crash, or export no audio classes. The
  scanner contains these failures in a child process and reports them, but it
  cannot repair the plug-in.
- The release is Windows x64 only and requires both executables to remain in the
  same directory.

# VST3 Plugin Scanner for Windows

Technical proof of concept for a reliable native VST3 inventory scanner. The
current implementation uses the official Steinberg VST3 SDK and treats a VST3
module as a container for zero, one, or multiple exported factory classes.

The previous passive C++ scanner remains available unchanged under the
`cpp-v1.1.0-pre-restart` tag. Its heuristics and Windows file-version data are
not used by the current scanner.

## Current state: validated VST3 inventory (2.4.0)

This branch builds an isolated x64 metadata probe and a native Win32 GUI:

```text
Vst3MetadataProbe.exe "C:\Path\Plugin.vst3"
Vst3ProbeGui.exe
```

The GUI scans individual modules or entire folders. Folder scans recognize
`.vst3` bundle directories as single modules. Every uncached candidate runs in
its own probe process. The initial attempt is limited to 15 seconds; a timeout
causes exactly one retry with a 30-second limit.

Version 2.4.0 provides:

- strict JSON protocol validation and `protocol_error` for empty output;
- a sortable inventory table for `Audio Module Class` entries;
- CID-based duplicate detection across different module paths;
- an optional single-file cache, disabled by default, stored as
  `vst3_scanner_cache.json` next to the GUI executable and keyed by a content
  fingerprint of the module binaries;
- statically linked MSVC runtimes for portable release binaries;
- matching GUI and CSV columns, with UTF-8 BOM and semicolon separators;
- CSV formula-injection protection for untrusted plug-in metadata;
- structured JSON export;
- expansion of multi-plug-in modules such as WaveShells into one row per audio
  class;
- an explicit version source: `VST3 factory` or `Not reported`;
- strict consistency checks for class count, class index, factory interface,
  CID, and `versionMissing`;
- exactly one inventory row per audio CID and module, including faulty factory
  responses;
- issue rows for load errors, `no_classes`, timeouts, and file-system warnings;
- bounded process waits instead of blocking pipe-reader threads;
- restricted handle inheritance and dedicated diagnostics for crashed probes;
- a Windows manifest for Common Controls v6, Per-Monitor V2 DPI awareness, and
  long-path support.

Controller, compatibility, and ARA helper classes are not counted as plug-ins.
An empty class vendor may only be replaced by the vendor from the same VST3
factory. Version strings are never changed heuristically. `VST3 factory` means
that the displayed raw value came directly from `IPluginFactory3` or
`IPluginFactory2`; it does not indicate whether a newer version is available
from the vendor.

The probe:

- examines exactly one module per process;
- loads it through Steinberg's official Windows hosting loader;
- queries only the plug-in factory and its metadata;
- uses `IPluginFactory3`, falling back to `IPluginFactory2`, then
  `IPluginFactory`;
- never creates a plug-in instance or calls `initialize`, audio, or GUI methods;
- writes exactly one UTF-8 JSON document to `stdout`;
- writes human-readable diagnostics only to `stderr`;
- preserves empty or unusual version strings exactly as reported.

Neither the Steinberg VST3 SDK nor the Microsoft Visual C++ Redistributable is
required at runtime. `Vst3ProbeGui.exe` and `Vst3MetadataProbe.exe` must remain
in the same directory.

See [GUI, inventory, and folder scanning](docs/RUDIMENTARY_GUI.md) for the GUI
and cache details.

## Requirements

- Windows 10 or Windows 11 x64
- Visual Studio 2022 Build Tools with MSVC v143 and a Windows SDK
- CMake 3.25 or newer
- Git with submodule support

The SDK is included as a recursive Git submodule pinned to
`v3.8.0_build_66` (`9fad9770f2ae8542ab1a548a68c1ad1ac690abe0`).

## Clean checkout

```powershell
git clone --recurse-submodules https://github.com/sscheidl/vst3-plugin-scanner.git
cd vst3-plugin-scanner
```

For an existing checkout:

```powershell
git submodule update --init --recursive
```

## Build

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Output:

```text
build\bin\Release\Vst3MetadataProbe.exe
build\bin\Release\Vst3ProbeGui.exe
```

Both executables must remain in the same directory.

## Process protocol

The current JSON schema version is `2`. A successful response contains module
data and one entry for every class index reported by the factory. Important raw
fields include `version` and `sdkVersion`; internally they are named
`ClassVersionRaw` and `SdkVersionRaw`.

```json
{
  "schemaVersion": 2,
  "status": "ok",
  "module": {
    "path": "C:\\Program Files\\Common Files\\VST3\\Example.vst3",
    "factoryVendor": "Example Audio",
    "classCount": 1,
    "probeDurationMs": 12
  },
  "classes": [
    {
      "index": 0,
      "cid": "00112233445566778899AABBCCDDEEFF",
      "category": "Audio Module Class",
      "name": "Example",
      "vendor": "Example Audio",
      "version": "2.4.1",
      "sdkVersion": "VST 3.7.9",
      "factoryInterface": 3,
      "isAudioPlugin": true,
      "versionMissing": false
    }
  ],
  "diagnostic": ""
}
```

See [Phase 1: Vst3MetadataProbe](docs/PHASE_1_PROBE.md) for the complete field
and status reference.

## License

The SDK sources remain in the official Steinberg submodule and are governed by
its license files. The scanner does not copy or modify plug-in files.

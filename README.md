# Windows VST Plugin Scanner

Native Windows application for scanning installed VST2, VST3, CLAP and AAX plugins without loading or initializing plugin binaries.

![Preview of the Windows VST Plugin Scanner](https://taureon-music.de/images/taureon/dev_lab/VST3-Plugin-Scanner.png)

## Features

- Win32 GUI with Start/Stop scan buttons.
- Separate Export button; reports are written only after a completed scan and explicit export action.
- VST2, VST3, CLAP, AAX and custom scan path selectors.
- Worker-thread scan so the GUI stays responsive.
- Stop flag for cancellable scans.
- Progress bar, status output, log output and result preview table.
- Resizable/maximizable window and sortable result table.
- Recursive scan for:
  - VST2: `*.dll`
  - VST3: `*.vst3` files and `.vst3` bundle directories
  - CLAP: `*.clap`
  - AAX: `*.aaxplugin` bundle directories
- Metadata from Windows version resources:
  - `FileDescription`
  - `ProductName`
  - `CompanyName`
  - `FileVersion`
  - `ProductVersion`
- Deterministic version-source priority and visible provenance:
  - VST3 `moduleinfo.json`
  - Windows `ProductVersion`
  - Windows `FileVersion`
  - numeric `VS_FIXEDFILEINFO`
  - conservative filename fallback
- Numeric version normalization/comparison (`1.10` sorts after `1.9`).
- Scan summary counts reliable, heuristic and missing versions separately.
- Conservative fallback to filename/folder name when metadata is missing.
- Conservative local category inference for common plugin types such as Instrument, Reverb, Compressor, EQ, Delay and Metering.
- Duplicate detection for possible cross-format pairs.
- Duplicate detection is intentionally cross-format only; same-format duplicates are kept visible as separate installs.
- Duplicate summaries distinguish groups, marked entries, and VST2 entries that can be deleted by the cleanup action.
- Reports:
  - HTML
  - CSV with UTF-8 BOM and semicolon separator
  - TXT

## Default Paths

- VST2: `C:\Program Files\Vstplugins`
- VST3: `C:\Program Files\Common Files\VST3`
- CLAP: `C:\Program Files\Common Files\CLAP`
- AAX: `C:\Program Files\Common Files\Avid\Audio\Plug-Ins`

## Build With Visual Studio

1. Open `VstPluginScanner.sln` in Visual Studio 2022.
2. Select `Release` and `x64`.
3. Build the solution.
4. The executable is created under:

```text
x64\Release\VstPluginScanner.exe
```

The project uses the Visual Studio 2022 `v143` toolset, C++20 and the Windows 10/11 SDK.
The executable embeds Windows version information `1.1.0.0`.

Or run:

```powershell
.\build_release.ps1
```

## Build With CMake

If CMake is installed:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is created under:

```text
build\Release\VstPluginScanner.exe
```

## Safety Notes

- Plugin DLLs are never loaded with `LoadLibrary` in the scanner process.
- The scanner reads only filesystem metadata and Windows version resources.
- A scan never changes plug-in files. Explicit cleanup commands move selected files to the Windows Recycle Bin after confirmation.
- No registry write access is used.
- Access-denied and broken files are logged and skipped safely.

## Steinberg VST3 SDK Preparation

- The official SDK is available at `https://github.com/steinbergmedia/vst3sdk` under the MIT license.
- `IVst3SdkProbe` is the integration seam for a future SDK-backed helper.
- The current implementation is a disabled stub and does not load plug-ins.
- Any future SDK probe must run out of process with timeout/crash isolation.
- CMake can validate a recursive SDK checkout without enabling plug-in loading:

```powershell
cmake -S . -B build -A x64 `
  -DVST3_SCANNER_PREPARE_STEINBERG_SDK=ON `
  -DVST3_SDK_ROOT=C:\path\to\vst3sdk
```

See `docs/VST3_SDK_INTEGRATION.md` for the integration plan.

## Project Structure

```text
include/
  DuplicateDetector.h
  MetadataReader.h
  PluginRecord.h
  ReportWriter.h
  ScannerEngine.h
  StringUtil.h
  VersionUtil.h
  Vst3SdkProbe.h
src/
  DuplicateDetector.cpp
  MetadataReader.cpp
  ReportWriter.cpp
  ScannerEngine.cpp
  StringUtil.cpp
  VersionUtil.cpp
  Vst3SdkProbeStub.cpp
  main.cpp
tests/
  MetadataReaderTests.cpp
  VersionUtilTests.cpp
```

# Windows VST Plugin Scanner

Native Windows application for scanning installed VST2, VST3, CLAP and AAX plugins without loading or initializing plugin binaries.

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
The executable embeds Windows version information `1.0.0.0`.

Or run:

```powershell
.\build_release.ps1
```

## Build With CMake

If CMake is installed:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The executable is created under:

```text
build\Release\VstPluginScanner.exe
```

## Safety Notes

- Plugin DLLs are never loaded with `LoadLibrary`.
- The scanner reads only filesystem metadata and Windows version resources.
- No plugin files are modified or deleted.
- No registry write access is used.
- Access-denied and broken files are logged and skipped safely.

## Later Experiments

- Steinberg VST 3 SDK is officially available from Steinberg and the public GitHub repository `steinbergmedia/vst3sdk`.
- The SDK can be useful later for optional VST3-specific inspection experiments, but the current scanner intentionally avoids loading or initializing plugin binaries.

## Project Structure

```text
include/
  DuplicateDetector.h
  MetadataReader.h
  PluginRecord.h
  ReportWriter.h
  ScannerEngine.h
  StringUtil.h
src/
  DuplicateDetector.cpp
  MetadataReader.cpp
  ReportWriter.cpp
  ScannerEngine.cpp
  StringUtil.cpp
  main.cpp
```

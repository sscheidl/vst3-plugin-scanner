# Windows VST Plugin Scanner - current state

Date: 2026-09-12
Version: 1.2.0.0

## Summary

Native Windows application in C++20/Win32 for Windows 11 x64. It scans VST2,
VST3, CLAP and AAX without loading or initializing plug-in binaries in the
scanner process. The user interface, all reports and the log are English only.

Repository: `https://github.com/sscheidl/vst3-plugin-scanner`

## Version resolution

Priority of the installed version number:

1. Future isolated VST3 SDK probe (currently a disabled stub).
2. Top-level `Version` from the VST3 `moduleinfo.json`.
3. Windows `ProductVersion`.
4. Windows `FileVersion`.
5. Numeric product/file version from `VS_FIXEDFILEINFO`.
6. Unique version pattern in the file name, flagged as a heuristic.
7. A user rule or a manual edit can override the result deliberately.

The GUI and the reports show the version source. The summary separates reliably
detected, heuristic and missing versions. Version values are normalized and
compared numerically.

## Audit and hardening 2026-08-01

- Buffer overrun in UTF-8/UTF-16 conversion fixed.
- Invalid UTF-8 sequences are rejected in a controlled way.
- `VERSIONINFO` strings are read within the reported buffer size.
- All present language/code page tables are searched.
- Numeric `VS_FIXEDFILEINFO` fallback added.
- `moduleinfo.json` is evaluated structurally; class versions can no longer
  replace the top-level module version by accident.
- JSON5 comments and trailing commas are tolerated for the relevant fields.
- Metadata files are capped at 4 MiB.
- VST3 binary resolution is deterministic and knows `x86_64-win`, `x64-win`,
  `arm64ec-win`, `arm64-win`, `x86-win` and `arm-win`.
- The VST2 PE check uses a read-only file mapping instead of a full file copy.
- PE headers, RVA conversion and export tables are hardened against overflows.
- Worker exceptions and thread start failures are handled in the GUI.
- Failed `PostMessage` calls no longer leak heap memory.

## Review and fixes 2026-09-12

- **One rules file.** The scan used to apply `plugin_rules_userprefs.json` next
  to the executable while the GUI edited and saved a file found in the exe
  directory, the working directory or two levels up. The GUI now resolves the
  path once and passes it to the engine through `ScanOptions::rulesPath`.
- **Warnings carry a code, not a string.** `WarningCode` in `PluginRecord.h`
  replaces the exact German text comparisons that the preference layer used to
  perform against text produced by the metadata reader. Display wording is now a
  pure UI concern.
- **Report and rules writing can no longer produce empty files.** `WideToUtf8`
  returns an empty string for unpaired surrogates; both writers used to treat
  that as a successful empty write. Conversion now happens before the file is
  opened and a failure is reported.
- **Deleting really uses the Recycle Bin.** `SHFileOperation` with
  `FOF_ALLOWUNDO` falls back to a permanent delete when an item cannot be
  recycled. Replaced by `IFileOperation` with `FOFX_RECYCLEONDELETE`, which
  fails instead of destroying the file.
- **Inline editor lifetime.** The editor is no longer destroyed from inside its
  own `WM_KILLFOCUS` handler; it is detached, hidden and destroyed from the
  message loop.
- **Rule matching no longer searches the whole path string.** Match tokens are
  tested as substrings against plug-in name, file name and file stem, but folder
  names are matched as whole components (substring only from five characters).
  Short tokens such as `bx` or `sol` previously matched almost any install path.
- **VST2 detection.** `VSTPluginMain` is accepted unconditionally. Legacy
  `main`/`main_plugin` is accepted only if the module does not also export COM
  registration entry points.
- **CSV formula injection.** Fields starting with `=`, `+`, `-` or `@` are
  quoted and prefixed, so a crafted plug-in name cannot become a spreadsheet
  formula.
- Application manifest added: ComCtl32 v6 visual styles, long path support,
  Windows 10/11 compatibility. Controls now use the system message font.
- Smaller items: dead `if`/`else` branch in `InferManufacturerFromPath` removed,
  redundant warning counting in `ScannerEngine` removed (all counters come from
  `BuildSummary`), overlapping creation coordinates of the action buttons fixed,
  queued worker notifications are drained on shutdown, `schemaVersion` aligned to
  `0.3-userprefs` across the app template and the shipped rules file.

## Tests and build

- Visual Studio release build: successful, 0 warnings, 0 errors.
- CTest `VersionUtilTests`: successful.
- CTest `MetadataReaderTests`: successful.
- Covered: parsing, normalization, comparison, Windows resources, VST3 module
  metadata and the file name fallback.

Build:

```powershell
.\build_release.ps1
```

Tests:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Steinberg SDK

The official SDK layout can already be validated optionally through CMake.
`IVst3SdkProbe` separates the scanner from a later implementation. The real
probe must be built as a separate process with a timeout and crash isolation.

Details: `docs/VST3_SDK_INTEGRATION.md`

## Remaining limits

- The real Steinberg SDK probe is not implemented yet.
- `moduleinfo.json` is optional and missing in many older plug-ins.
- Vendors may maintain Windows resources incorrectly or not at all.
- A VST3 bundle can contain several plug-in classes with different versions;
  the scanner still produces one record per bundle.
- The local parser covers the required JSON5 fields but is not a complete
  general JSON5 parser.
- Values inside `pluginRules` that are not strings or string arrays (numbers,
  booleans) are written back as `null`. All shipped rules use strings only, so
  this is currently latent.
- The application is deliberately declared DPI unaware because the layout uses
  fixed pixel metrics. Enabling PerMonitorV2 requires a scaled layout pass first.
- An online check against vendor websites is not implemented yet.
- File name versions are intentionally heuristic and are not treated as reliable.

## Next reasonable stages

1. Measure from scan reports which vendors/plug-ins still lack versions.
2. Implement an isolated `Vst3MetadataProbe.exe` with the Steinberg SDK.
3. Extend the data model to several VST3 classes per bundle.
4. Build vendor adapters for online versions with cache, rate limit and source URL.
5. Compare installed and available versions with `CompareVersionStrings`.

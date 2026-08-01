# Steinberg VST3 SDK integration plan

## Current boundary

The scanner does not load or initialize plug-in binaries. It obtains versions from
`moduleinfo.json`, Windows version resources, numeric `VS_FIXEDFILEINFO`, user rules,
and finally a clearly marked filename heuristic.

`IVst3SdkProbe` is the stable boundary for adding SDK-backed metadata later. The
default implementation is disabled and therefore preserves the current safety model.

## Why the SDK probe must be a separate process

Reading the VST3 factory and class information requires loading third-party code.
Plug-ins can be defective, block during initialization, spawn UI, or terminate the
process. The GUI process must therefore never perform this work directly.

The planned `Vst3MetadataProbe.exe` should:

1. Accept exactly one bundle path and a machine-readable request.
2. Load only that plug-in and query factory/class metadata through the VST3 SDK.
3. Return JSON containing module and class name, vendor, version, CID, category,
   subcategories, SDK version, and diagnostics.
4. Run inside a Windows Job Object and be terminated after a strict timeout.
5. Never inherit unnecessary handles and never write to the scanned bundle.
6. Be disposable: a crash affects one plug-in, not the scan or GUI.

## Version semantics

VST3 exposes several values that are not always identical:

- Module version from top-level `moduleinfo.json`.
- Class version for each `Audio Module Class`.
- Windows `ProductVersion` and `FileVersion` of the architecture binary.
- Numeric versions in `VS_FIXEDFILEINFO`.

The future probe should return all evidence instead of overwriting it. For a bundle
with one audio class, the class version is usually the most useful installed product
version. A bundle can expose multiple plug-in classes, so the data model will later
need either one record per class or a module record with child classes.

## Build preparation

The CMake option below validates that a recursive official SDK checkout has the
expected interface and ModuleInfoLib layout:

```powershell
cmake -S . -B build -A x64 `
  -DVST3_SCANNER_PREPARE_STEINBERG_SDK=ON `
  -DVST3_SDK_ROOT=C:\path\to\vst3sdk
```

This option deliberately does not load or link plug-ins yet. The helper executable
and its process protocol should be implemented and tested before replacing
`Vst3SdkProbeStub.cpp`.

Official references:

- https://github.com/steinbergmedia/vst3sdk
- https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/VST%2BModule%2BArchitecture/ModuleInfo-JSON.html
- https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BFormat.html

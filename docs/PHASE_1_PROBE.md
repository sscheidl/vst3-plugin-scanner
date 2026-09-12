# Phase 1: Vst3MetadataProbe

## Security boundary

`Vst3MetadataProbe.exe` is the only component that loads third-party VST3 code.
It processes exactly one path and then exits. The main application must not link
this code and starts a new probe process for every module.

The probe uses `VST3::Hosting::Module::create` and Steinberg's
`module_win32.cpp`. It queries only these metadata interfaces:

1. `IPluginFactory::getFactoryInfo`
2. `IPluginFactory::countClasses`
3. `IPluginFactory3::getClassInfoUnicode`
4. `IPluginFactory2::getClassInfo2` as the first fallback
5. `IPluginFactory::getClassInfo` as the final fallback

It never calls `createInstance`. No components, controllers, editors, or audio
processors are created or initialized.

## stdout and stderr

- `stdout`: exactly one compact UTF-8 JSON document followed by a newline
- `stderr`: human-readable diagnostics, when available
- Invalid UTF-8 bytes from third-party factory data are replaced with U+FFFD in JSON.
- JSON strings escape quotation marks, backslashes, and control characters.

## Status values

| Status | Meaning | Exit code |
| --- | --- | ---: |
| `ok` | Factory and all class information read successfully | 0 |
| `partial` | Module read, but at least one metadata call failed | 0 |
| `protocol_error` | Argument error or unexpected internal error | 2 |
| `not_vst3` | Candidate is not a VST3 module | 3 |
| `access_error` | Path could not be read | 3 |
| `wrong_architecture` | Module architecture does not match the x64 probe | 4 |
| `load_error` | Steinberg module loader could not load the module | 4 |
| `factory_missing` | No plug-in factory is available | 5 |
| `factory_error` | Factory returned invalid basic data | 5 |
| `no_classes` | Factory reports zero classes | 6 |
| `timeout` | Generated later by the supervising scanner | 7 |
| `crashed` | Generated later by the supervising scanner | 7 |

Phase 1 does not itself produce `timeout` or `crashed`, because only a supervising
parent process can determine those states reliably. A current loader failure is
intentionally preserved as `load_error`; later phases can further distinguish
`not_vst3`, `access_error`, and `wrong_architecture` from candidate and process data.

## JSON fields

### Module

| Field | Source |
| --- | --- |
| `path` | Unmodified UTF-8 command-line argument |
| `name` | `VST3::Hosting::Module::getName()` |
| `isBundle` | `VST3::Hosting::Module::isBundle()` |
| `factoryVendor` | `PFactoryInfo::vendor` |
| `factoryUrl` | `PFactoryInfo::url` |
| `factoryEmail` | `PFactoryInfo::email` |
| `factoryFlags` | `PFactoryInfo::flags` |
| `classCount` | `IPluginFactory::countClasses()` |
| `probeDurationMs` | Monotonic process timing around module load and factory query |

### Class

| Field | Source |
| --- | --- |
| `index` | Factory class index |
| `cid` | SDK `UID::toString()`, 32 uppercase hexadecimal characters |
| `cardinality` | ClassInfo cardinality |
| `category` | ClassInfo category |
| `name` | ClassInfo name |
| `classFlags` | ClassInfo flags; `0` for Factory1 |
| `subCategories` | Raw values split at the SDK `|` separator |
| `vendor` | ClassInfo vendor; no factory fallback in the probe |
| `version` | Raw ClassInfo version; no fallback or normalization |
| `sdkVersion` | Raw ClassInfo SDK version |
| `factoryInterface` | Successfully used interface: `3`, `2`, or `1` |
| `isAudioPlugin` | Exact comparison with `Audio Module Class` |
| `versionMissing` | `true` when the raw version string is empty |
| `diagnostic` | Error for this exact index |

If all three ClassInfo queries fail for an index, the index is not skipped.
Instead, `classes` contains an empty error record and the overall status is
`partial`.

## Verified Phase 1 test

The x64 release build was tested locally with `bitcrust.vst3`:

- Status: `ok`
- Factory vendor: `Anode Labs`
- Exported classes: `3`
- Successful factory interface per class: `IPluginFactory3`
- Reported class version: `1.0.8`
- Reported SDK version: `VST 3.7.12`
- Audio plug-in classes: `1`
- Duration of the final test run: `9 ms`

This is not a complete plug-in validation. It confirms module loading, factory
queries, the multi-class model, and the Phase 1 JSON protocol only.

## Current follow-up state

The probe has since been tested successfully with regular commercial VST3
modules and a Waves shell module. The GUI folder scanner is implemented and
continues to start a separate probe process for every candidate. Testing against
an official Steinberg sample remains an additional reference test.

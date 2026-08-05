# Phase 1: Vst3MetadataProbe

## Sicherheitsgrenze

`Vst3MetadataProbe.exe` ist die einzige Komponente, die fremden VST3-Code lädt.
Sie bearbeitet exakt einen Pfad und beendet sich danach. Die spätere Hauptanwendung
darf diesen Code nicht linken und startet für jedes Modul einen neuen Probe-Prozess.

Die Probe verwendet `VST3::Hosting::Module::create` und Steinbergs
`module_win32.cpp`. Sie fragt ausschließlich folgende Metadaten-Schnittstellen ab:

1. `IPluginFactory::getFactoryInfo`
2. `IPluginFactory::countClasses`
3. `IPluginFactory3::getClassInfoUnicode`
4. ersatzweise `IPluginFactory2::getClassInfo2`
5. ersatzweise `IPluginFactory::getClassInfo`

Sie ruft `createInstance` nicht auf. Es werden keine Komponenten, Controller,
Editoren oder Audioprozessoren erzeugt oder initialisiert.

## stdout und stderr

- `stdout`: genau ein kompaktes UTF-8-JSON-Dokument und abschließender Zeilenumbruch
- `stderr`: Diagnose in menschenlesbarer Form, falls vorhanden
- Ungültige UTF-8-Bytes aus fremden Factory-Daten werden im JSON durch U+FFFD ersetzt.
- JSON-Strings maskieren Anführungszeichen, Backslashes und Steuerzeichen.

## Statuswerte

| Status | Bedeutung | Exitcode |
| --- | --- | ---: |
| `ok` | Factory und alle Klasseninformationen gelesen | 0 |
| `partial` | Modul gelesen, mindestens ein Metadatenaufruf fehlgeschlagen | 0 |
| `protocol_error` | Argument- oder unerwarteter interner Fehler | 2 |
| `not_vst3` | Kandidat ist kein VST3-Modul | 3 |
| `access_error` | Pfad konnte nicht gelesen werden | 3 |
| `wrong_architecture` | Modul passt nicht zur x64-Probe | 4 |
| `load_error` | Steinberg-Modullader konnte das Modul nicht laden | 4 |
| `factory_missing` | Keine Plugin-Factory verfügbar | 5 |
| `factory_error` | Factory lieferte ungültige Basisdaten | 5 |
| `no_classes` | Factory meldet null Klassen | 6 |
| `timeout` | Wird später vom aufrufenden Scanner erzeugt | 7 |
| `crashed` | Wird später vom aufrufenden Scanner erzeugt | 7 |

Phase 1 erzeugt selbst noch keine Statuswerte `timeout` oder `crashed`, weil diese
nur ein überwachender Elternprozess zuverlässig feststellen kann. `not_vst3`,
`access_error` und `wrong_architecture` werden in einer späteren Phase anhand des
Kandidaten- und Prozessstatus weiter differenziert; ein aktueller Ladefehler bleibt
bewusst als `load_error` erhalten.

## JSON-Felder

### Modul

| Feld | Quelle |
| --- | --- |
| `path` | unverändertes UTF-8-Kommandozeilenargument |
| `name` | `VST3::Hosting::Module::getName()` |
| `isBundle` | `VST3::Hosting::Module::isBundle()` |
| `factoryVendor` | `PFactoryInfo::vendor` |
| `factoryUrl` | `PFactoryInfo::url` |
| `factoryEmail` | `PFactoryInfo::email` |
| `factoryFlags` | `PFactoryInfo::flags` |
| `classCount` | `IPluginFactory::countClasses()` |
| `probeDurationMs` | monotone Prozessmessung um Laden und Factory-Abfrage |

### Klasse

| Feld | Quelle |
| --- | --- |
| `index` | Factory-Klassenindex |
| `cid` | SDK-`UID::toString()`, 32 großgeschriebene Hex-Zeichen |
| `cardinality` | ClassInfo-Cardinality |
| `category` | ClassInfo-Category |
| `name` | ClassInfo-Name |
| `classFlags` | ClassInfo-Flags, bei Factory1 `0` |
| `subCategories` | unveränderte, am SDK-Trennzeichen `|` getrennte Werte |
| `vendor` | ClassInfo-Vendor, kein Factory-Fallback |
| `version` | roher ClassInfo-Versionsstring, kein Fallback und keine Normalisierung |
| `sdkVersion` | roher ClassInfo-SDK-Versionsstring |
| `factoryInterface` | tatsächlich erfolgreiche Schnittstelle `3`, `2` oder `1` |
| `isAudioPlugin` | exakter Vergleich mit `Audio Module Class` |
| `versionMissing` | `true`, wenn der rohe Versionsstring leer ist |
| `diagnostic` | Fehler für genau diesen Index |

Falls alle drei ClassInfo-Abfragen an einem Index fehlschlagen, wird der Index nicht
übersprungen. Stattdessen enthält `classes` einen leeren Fehlerdatensatz und der
Gesamtstatus lautet `partial`.

## Verifizierter Phase-1-Test

Der x64-Release-Build wurde lokal mit `bitcrust.vst3` getestet:

- Status: `ok`
- Factory-Hersteller: `Anode Labs`
- exportierte Klassen: `3`
- erfolgreiche Factory-Schnittstelle je Klasse: `IPluginFactory3`
- gemeldete Klassenversion: `1.0.8`
- gemeldete SDK-Version: `VST 3.7.12`
- Audio-Plugin-Klassen: `1`
- Laufzeit des abschließenden Testlaufs: `9 ms`

Dieser Befund ist keine vollständige Pluginvalidierung. Er bestätigt nur Laden,
Factory-Abfrage, Multi-Class-Modell und JSON-Protokoll von Phase 1.

## Folgestand

Die Probe wurde inzwischen erfolgreich mit normalen kommerziellen VST3-Modulen und
einem Waves-Shell-Modul geprüft. Der GUI-Batchscanner ist implementiert und startet
weiterhin für jeden Kandidaten einen getrennten Probe-Prozess. Der noch ausstehende
offizielle Steinberg-Beispieltest bleibt ein zusätzlicher Referenztest.

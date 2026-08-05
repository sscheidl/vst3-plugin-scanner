# VST3 Plugin Scanner for Windows

Technische Machbarkeitsstudie für einen zuverlässigen, nativen VST3-Inventarscanner.
Der Neustart verwendet das offizielle Steinberg VST3 SDK und behandelt ein VST3-Modul
als Container für null, eine oder mehrere exportierte Factory-Klassen.

Der vorherige passive C++-Scanner ist unverändert über den Tag
`cpp-v1.1.0-pre-restart` verfügbar. Seine Heuristiken und Windows-Dateiversionen
fließen nicht in die Ergebnisse dieser Studie ein.

## Aktueller Stand: Inventar und optionaler Cache (2.3.0)

Der Branch baut eine isolierte x64-Metadatenprobe und eine native Win32-GUI:

```text
Vst3MetadataProbe.exe "C:\Pfad\Plugin.vst3"
Vst3ProbeGui.exe
```

Die GUI scannt einzelne Module oder komplette Ordner. `.vst3`-Bundle-Verzeichnisse
werden vom Ordnerscan automatisch als einzelne Module erkannt. Jeder nicht
zwischengespeicherte Kandidat laeuft in einem eigenen Probe-Prozess. Der erste
Versuch ist auf 15 Sekunden begrenzt; nur nach Timeout folgt genau eine
Wiederholung mit maximal 30 Sekunden.

Version 2.3.0 bietet:

- strikte Validierung des JSON-Protokolls und `protocol_error` bei leerer Ausgabe;
- eine sortierbare Tabelle fuer `Audio Module Class`-Eintraege;
- CID-basierte Dublettenerkennung ueber verschiedene Modulpfade;
- einen standardmaessig deaktivierten, optionalen Einzeldatei-Cache
  `vst3_scanner_cache.json` neben der EXE mit Inhaltsfingerprint der Modulbinaerdatei;
- statisch eingebundene MSVC-Runtimes fuer eine portable Release-Ausgabe;
- harmonisierte Fenster- und CSV-Spalten mit UTF-8-BOM und Semikolon;
- strukturierten JSON-Export;
- aufgeloeste Multi-Plugin-Module wie WaveShells als eine Zeile je Audioklasse;
- Problemzeilen fuer Ladefehler, `no_classes`, Timeouts und Dateisystemwarnungen;
- begrenztes Prozess-Warten mit Fehlermeldung statt blockierender Pipe-Threads;
- eingeschraenkte Handle-Vererbung sowie eigene Diagnosen fuer abgestuerzte Probes.

Controller-, Compatibility- und ARA-Hilfsklassen werden nicht als Plugins gezaehlt.
Ein leerer Klassenhersteller darf ausschliesslich durch den Hersteller derselben
VST3-Factory ersetzt werden. Versionswerte werden nie heuristisch veraendert.

Die Probe:

- untersucht genau ein Modul pro Prozess;
- laedt das Modul mit Steinbergs offiziellem Windows-Hosting-Loader;
- ruft nur die Plugin-Factory und deren Metadaten ab;
- verwendet `IPluginFactory3`, ersatzweise `IPluginFactory2`, ersatzweise `IPluginFactory`;
- erzeugt keine Plugininstanz und ruft weder `initialize` noch Audio- oder GUI-Funktionen auf;
- gibt genau ein UTF-8-JSON-Dokument auf `stdout` aus;
- schreibt Diagnosen ausschliesslich auf `stderr`;
- laesst leere oder ungewoehnliche Versionsstrings unveraendert sichtbar.

Zur Ausfuehrung werden weder das Steinberg VST3 SDK noch das Microsoft Visual C++
Redistributable benoetigt. `Vst3ProbeGui.exe` und `Vst3MetadataProbe.exe` muessen
gemeinsam im selben Verzeichnis bleiben.

Weitere GUI- und Cache-Details stehen in
[`docs/RUDIMENTARY_GUI.md`](docs/RUDIMENTARY_GUI.md).
## Voraussetzungen

- Windows 10 oder Windows 11 x64
- Visual Studio 2022 Build Tools mit MSVC v143 und Windows SDK
- CMake 3.25 oder neuer
- Git mit Submodule-Unterstützung

Das SDK ist als rekursives Git-Submodule eingebunden und auf
`v3.8.0_build_66` (`9fad9770f2ae8542ab1a548a68c1ad1ac690abe0`) fixiert.

## Sauberer Checkout

```powershell
git clone --recurse-submodules https://github.com/sscheidl/vst3-plugin-scanner.git
cd vst3-plugin-scanner
```

Bei einem bereits vorhandenen Checkout:

```powershell
git submodule update --init --recursive
```

## Build

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Ergebnis:

```text
build\bin\Release\Vst3MetadataProbe.exe
build\bin\Release\Vst3ProbeGui.exe
```

Beide EXE-Dateien müssen im selben Verzeichnis bleiben. Weitere Details stehen in
[`docs/RUDIMENTARY_GUI.md`](docs/RUDIMENTARY_GUI.md).

## Prozessprotokoll

Das JSON-Schema hat aktuell Version `2`. Eine erfolgreiche Antwort enthält
Moduldaten sowie einen Eintrag für jeden von der Factory gemeldeten Klassenindex.
Wichtige unverfälschte Felder sind `version` und `sdkVersion`; intern heißen sie
`ClassVersionRaw` und `SdkVersionRaw`.

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

Die vollständige Feld- und Statusbeschreibung steht in
[`docs/PHASE_1_PROBE.md`](docs/PHASE_1_PROBE.md).

## Lizenz

Die SDK-Quellen bleiben im offiziellen Steinberg-Submodule und unterliegen dessen
Lizenzdateien. Der Scanner kopiert oder verändert keine Plugin-Dateien.

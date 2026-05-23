# Windows VST Plugin Scanner - aktueller Stand

Stand: 2026-05-23

## Kurzfassung

Native Windows-Anwendung in C++/Win32 zum Scannen installierter VST2-, VST3-, CLAP- und AAX-Plugins. Die Anwendung laedt oder initialisiert keine Plugin-Binaries, sondern liest Dateisystemdaten, Windows-Versioninformationen und bei VST3 optional `moduleinfo.json`.

Aktueller Release-Build:

```text
D:\Eigene Dateien\Eigene Dokumente\Playground\vst_plugin_scanner_cpp\x64\Release\VstPluginScanner.exe
```

Version in EXE-Ressourcen:

```text
1.0.0.0
```

Build-Script:

```powershell
.\build_release.ps1
```

## Aktuell implementiert

- Win32-GUI mit Start/Stop-Scan.
- Resizable/maximizable Fenster.
- Worker-Thread fuer Scan, GUI bleibt waehrenddessen bedienbar.
- Stop-Flag fuer abbrechbare Scans.
- Fortschrittsbalken, Statuszeile, Logfeld und Ergebnisliste.
- Sortierbare Ergebnis-Tabelle per Spaltenklick.
- Export erfolgt erst nach abgeschlossenem Scan und explizitem Klick auf `Export`.
- Exportformate: HTML, CSV, TXT.
- HTML-Report mit lokaler Tabelle und Zusammenfassung.
- CSV mit UTF-8 BOM und Semikolon fuer deutsche Excel-Kompatibilitaet.
- TXT-Report als lesbare strukturierte Liste.
- Kontextmenue auf Ergebniszeilen:
  - Zielpfad im Explorer oeffnen.
  - Datei bzw. Bundle loeschen.
- Cleanup-Buttons:
  - `Del VST2 Dup`: loescht nur VST2-Eintraege, die als moegliche Dublette erkannt wurden.
  - `Del CLAP`: loescht CLAP-Dateien.
  - `Del AAX`: loescht AAX-Bundles.
- Loeschaktionen fragen vorher nach und nutzen den Windows-Papierkorb.

## Scan-Ziele

Standardpfade:

```text
VST2: C:\Program Files\Vstplugins
VST3: C:\Program Files\Common Files\VST3
CLAP: C:\Program Files\Common Files\CLAP
AAX:  C:\Program Files\Common Files\Avid\Audio\Plug-Ins
```

Unterstuetzte Formate:

- VST2: `*.dll`
- VST3: `*.vst3` Dateien und `.vst3` Bundle-Ordner
- CLAP: `*.clap`
- AAX: `.aaxplugin` Bundle-Ordner

## Sicherheit / Design-Entscheidungen

- VST2-DLLs werden nicht per `LoadLibrary` geladen.
- VST2-Erkennung prueft statisch die PE-Exporttabelle auf `VSTPluginMain` oder `main`.
- VST3/AAX-Bundles werden rekursiv durchsucht, ohne Plugin-Code auszufuehren.
- Keine Registry-Schreibzugriffe.
- Plugin-Dateien werden beim Scan nicht veraendert.
- Defekte, gesperrte oder nicht lesbare Dateien werden protokolliert und uebersprungen.
- Beim Schliessen des Fensters waehrend eines Scans wird der Worker-Thread sauber gestoppt und gejoint.

## Metadaten

Ausgelesene Windows-Versioninformationen:

- `FileDescription`
- `ProductName`
- `CompanyName`
- `FileVersion`
- `ProductVersion`

VST3-Zusatzquelle:

- `moduleinfo.json`
- Aktuell eigener konservativer JSON-String-Parser, kein externer JSON-Parser.

Fallback:

- Pluginname aus Dateiname bzw. Bundle-Name.
- Hersteller vorsichtig aus bekannten Hersteller-Tokens und Ordnerstruktur.
- Keine aggressive Heuristik, die Hersteller oder Pluginname frei erfindet.

## Kategorie-Erkennung

Es gibt eine konservative lokale Heuristik fuer Plugin-Kategorien, z. B.:

- Instrument
- Reverb
- Compressor
- EQ
- Delay
- Metering

Diese Erkennung basiert aktuell auf Namen/Metadaten und ist bewusst vorsichtig. Eine spaetere bessere Loesung waere eine lokale Hersteller-/Plugin-Datenbank oder optional ein SDK-basierter Ansatz.

## Dubletten-Erkennung

Dubletten-Erkennung ist absichtlich cross-format:

- VST2/VST3
- VST2/CLAP
- VST3/CLAP
- usw.

Same-format-Dubletten werden aktuell nicht als Dublettengruppe markiert.

Normalisierung basiert auf:

- Herstellername, sofern vorhanden.
- Pluginname.
- Dateiname ohne Erweiterung.

Wichtige Korrektur:

- Die GUI unterscheidet jetzt zwischen `Dubletten-Gruppen`, `Eintraege` und `VST2 loeschbar`.
- Beispiel: 41 VST2/VST3-Paare erscheinen als 41 Gruppen und 82 Eintraege.
- `VST2 loeschbar` zeigt nur die VST2-Eintraege, die der Cleanup-Button entfernen wuerde.

## Zuletzt beobachtete reale Scan-Zahlen

Vom Nutzer gemeldet:

```text
478 VST3
71 VST2
1 CLAP
82 Dubletten-Eintraege nach alter Anzeige
AAX entfernt
```

Interpretation:

- Die alte Anzeige meinte markierte Eintraege, nicht Gruppen.
- 82 Eintraege koennen z. B. 41 VST2/VST3-Paare bedeuten.
- Der aktuelle Build macht diese Unterscheidung sichtbar.

## Wichtige Dateien

```text
include\PluginRecord.h
include\ScannerEngine.h
include\MetadataReader.h
include\DuplicateDetector.h
include\ReportWriter.h
include\StringUtil.h

src\main.cpp
src\ScannerEngine.cpp
src\MetadataReader.cpp
src\DuplicateDetector.cpp
src\ReportWriter.cpp
src\StringUtil.cpp

VstPluginScanner.sln
VstPluginScanner.vcxproj
VstPluginScanner.rc
CMakeLists.txt
build_release.ps1
README.md
```

## Build-Umgebung

Installiert und verwendet:

- Visual Studio Build Tools 2022
- MSVC C++ Build Tools
- Desktop development with C++
- Windows SDK
- CMake tools for Windows

Bekannter MSBuild-Pfad:

```text
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
```

## Letzter Build

Der letzte Release-Build lief erfolgreich:

```text
0 Warnungen
0 Fehler
```

EXE:

```text
D:\Eigene Dateien\Eigene Dokumente\Playground\vst_plugin_scanner_cpp\x64\Release\VstPluginScanner.exe
```

## Smoke-Test

Ein kleiner Test mit kuenstlichen Plugin-Dateien wurde ausgefuehrt:

- 1 Dummy-VST2-DLL mit `VSTPluginMain` Export.
- 1 gleichnamiges VST3-Bundle mit `moduleinfo.json`.

Erwartetes und erreichtes Ergebnis:

```text
records=2
vst2=1
vst3=1
groups=1
entries=2
vst2del=1
```

## Bekannte Einschraenkungen

- Kein echtes Steinberg VST SDK eingebunden.
- Keine Plugin-Binaries werden geladen, daher sind manche Metadaten nicht verfuegbar.
- Kategorie-Erkennung ist heuristisch und nicht vollstaendig.
- Hersteller-Erkennung kann bei fehlenden Metadaten weiterhin unvollstaendig sein.
- VST3-`moduleinfo.json` wird mit eigenem Parser gelesen; ein echter JSON-Parser wie `nlohmann/json` waere robuster.
- Keine Undo-Funktion innerhalb der App; Loeschungen gehen aber in den Windows-Papierkorb.
- Keine persistente Settings-Datei fuer zuletzt genutzte Pfade/Optionen.

## Sinnvolle naechste Schritte

1. Hersteller-Heuristik verbessern:
   - Lokale Mapping-Datei `vendors.json`.
   - Bekannte Ordner-/Dateiname-Prefixes pro Hersteller.
   - Vom Nutzer editierbare Overrides.

2. Dubletten-Ansicht verbessern:
   - Filter `Nur Dubletten`.
   - Gruppierte Anzeige nach `duplicateGroupId`.
   - Vor Cleanup eine Vorschau der konkret zu loeschenden Dateien.

3. Settings speichern:
   - Letzte Pfade.
   - Letztes Exportformat.
   - Letzter Ausgabeort.
   - Fensterposition/-groesse.

4. Report verbessern:
   - HTML mit Suchfeld/Filter.
   - Separate Sektion fuer Dubletten.
   - Separate Sektion fuer Warnungen/Fehler.

5. Metadaten verbessern:
   - `nlohmann/json` fuer `moduleinfo.json`.
   - Optionale lokale Plugin-Datenbank fuer Kategorie und Hersteller.
   - Spaeter experimentell Steinberg VST3 SDK pruefen, aber weiterhin ohne Plugin-Initialisierung.

6. Sicherheit bei Cleanup weiter erhoehen:
   - Dry-run/Vorschau.
   - Backup-Liste der geloeschten Pfade.
   - Export der Cleanup-Aktion als Logdatei.

## Hinweis fuer neuen Chat

Wenn ein neuer Chat beginnt, diese Datei als Kontext angeben und sagen:

```text
Bitte lies STATUS.md im Projektordner. Arbeite am C++/Win32-Projekt `vst_plugin_scanner_cpp` weiter.
```


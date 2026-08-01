# Windows VST Plugin Scanner - aktueller Stand

Stand: 2026-08-01
Version: 1.1.0.0

## Kurzfassung

Native Windows-Anwendung in C++20/Win32 fuer Windows 11 x64. Sie scannt VST2,
VST3, CLAP und AAX, ohne Plugin-Binaries im Scannerprozess zu laden oder zu
initialisieren.

Repository: `https://github.com/sscheidl/vst3-plugin-scanner`

## Versionsaufloesung

Prioritaet der installierten Versionsnummer:

1. Zukuenftige isolierte VST3-SDK-Probe (aktuell deaktivierter Stub).
2. Top-Level-`Version` aus VST3 `moduleinfo.json`.
3. Windows `ProductVersion`.
4. Windows `FileVersion`.
5. Numerische Produkt-/Dateiversion aus `VS_FIXEDFILEINFO`.
6. Eindeutiges Versionsmuster im Dateinamen, als Heuristik markiert.
7. Benutzerregel oder manuelle Eingabe kann das Ergebnis gezielt ueberschreiben.

GUI und Reports zeigen die Versionsquelle. Die Zusammenfassung unterscheidet
zuverlaessig erkannte, heuristische und fehlende Versionen. Versionswerte werden
normalisiert und numerisch verglichen.

## Audit und Haertung 2026-08-01

- Speicherueberlauf in UTF-8-/UTF-16-Konvertierung behoben.
- Ungueltige UTF-8-Sequenzen werden kontrolliert abgewiesen.
- `VERSIONINFO`-Strings werden innerhalb der gemeldeten Puffergroesse gelesen.
- Alle vorhandenen Sprach-/Codepage-Tabellen werden durchsucht.
- Numerischer `VS_FIXEDFILEINFO`-Fallback ergaenzt.
- `moduleinfo.json` wird strukturell ausgewertet; Klassen-Versionen koennen die
  Top-Level-Modulversion nicht mehr versehentlich ersetzen.
- JSON5-Kommentare und nachgestellte Kommata werden fuer relevante Felder toleriert.
- Metadatendateien sind auf 4 MiB begrenzt.
- VST3-Binary-Aufloesung ist deterministisch und kennt `x86_64-win`, `x64-win`,
  `arm64ec-win`, `arm64-win`, `x86-win` und `arm-win`.
- VST2-PE-Pruefung nutzt Read-only File Mapping statt kompletter Dateikopie.
- PE-Header, RVA-Umrechnung und Exporttabellen sind gegen Ueberlaeufe gehaertet.
- Worker-Ausnahmen und Fehler beim Threadstart werden in der GUI behandelt.
- Fehlgeschlagene `PostMessage`-Aufrufe verlieren keinen Heap-Speicher.

## Tests und Build

- Visual-Studio-Release-Build: erfolgreich, 0 Warnungen, 0 Fehler.
- CTest `VersionUtilTests`: erfolgreich.
- CTest `MetadataReaderTests`: erfolgreich.
- Getestet werden Parsing, Normalisierung, Vergleich, Windows-Ressourcen,
  VST3-Modulmetadaten und Dateinamen-Fallback.

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

Die offizielle SDK-Struktur kann bereits optional ueber CMake validiert werden.
`IVst3SdkProbe` trennt den Scanner von einer spaeteren Implementierung. Die echte
Probe muss als separater Prozess mit Timeout und Crash-Isolation gebaut werden.

Details: `docs/VST3_SDK_INTEGRATION.md`

## Verbleibende Grenzen

- Die echte Steinberg-SDK-Probe ist noch nicht implementiert.
- `moduleinfo.json` ist optional und bei vielen aelteren Plugins nicht vorhanden.
- Hersteller koennen Windows-Ressourcen falsch oder gar nicht pflegen.
- Ein VST3-Bundle kann mehrere Plugin-Klassen mit verschiedenen Versionen enthalten;
  aktuell bleibt es ein Scanner-Datensatz pro Bundle.
- Der lokale Parser deckt die benoetigten JSON5-Felder ab, ist aber kein vollstaendiger
  allgemeiner JSON5-Parser.
- Eine Online-Pruefung auf Hersteller-Websites ist noch nicht implementiert.
- Dateinamen-Versionen sind bewusst nur Heuristik und gelten nicht als sicher.

## Naechste sinnvolle Etappen

1. Aus Scanreports messen, bei welchen Herstellern/Plugins Versionen noch fehlen.
2. Isolierten `Vst3MetadataProbe.exe` mit Steinberg SDK implementieren.
3. Datenmodell auf mehrere VST3-Klassen pro Bundle erweitern.
4. Herstelleradapter fuer Online-Versionen mit Cache, Rate-Limit und Quellen-URL bauen.
5. Installierte und verfuegbare Version mit `CompareVersionStrings` vergleichen.

# Projektstatus

Version: 2.2.3
Branch: `restart/vst3-sdk-probe`

## Aktiver Scanner

Der aktive Stand ist ein nativer Windows-x64-Scanner für VST3. Er besteht aus:

- `Vst3ProbeGui.exe`: Win32-Oberfläche, rekursiver Ordnerscan, Sortierung und Export;
- `Vst3MetadataProbe.exe`: isolierter Prozess für genau ein VST3-Modul;
- dem offiziellen Steinberg VST3 SDK als fixiertem Git-Submodule;
- einem streng validierten JSON-Protokoll zwischen GUI und Probe.

Die Probe fragt ausschließlich Factory- und ClassInfo-Metadaten ab. Sie erzeugt keine
Plugininstanz und verändert keine Plugin-Dateien. Version, Hersteller, Name, CID,
Kategorien und SDK-Version stammen direkt aus der VST3-Factory. Es gibt keine
Versions- oder Herstellerheuristik.

## Sicherheit und Verhalten

- Jedes Modul läuft in einem eigenen Windows Job Object.
- Erster Timeout: 15 Sekunden; genau eine Wiederholung mit maximal 30 Sekunden.
- Stoppen, Timeout und Fensterschließen beenden den gesamten Probe-Prozessbaum.
- Nur die drei Standard-Handles werden an den Probe-Prozess vererbt.
- Abstürze und widersprüchliche Prozessantworten werden gesondert diagnostiziert.
- `.vst3`-Dateien und Bundle-Verzeichnisse werden rekursiv erkannt.
- Verzeichnis-Symlinks werden nicht verfolgt.

## Cache und Export

Der optionale Cache ist standardmäßig deaktiviert. Wenn er aktiviert wird, liegt die
einzige Datei `vst3_scanner_cache.json` neben der GUI-EXE. Ein Cachetreffer setzt einen
unveränderten Pfad, unveränderte Dateigrößen und unveränderte Änderungszeiten voraus.
Nur erfolgreiche und erneut validierte Probe-Antworten werden gespeichert.

CSV wird mit UTF-8-BOM und Semikolon geschrieben. JSON bewahrt die Rohwerte der
Factory einschließlich Version, SDK-Version, Diagnose und Probe-Laufzeit.

## Historischer Stand

Der frühere passive C++-Scanner ist über den Tag `cpp-v1.1.0-pre-restart` erhalten.
Seine heuristischen Quellen und alten Projektdateien gehören nicht mehr zum aktiven
Branch.

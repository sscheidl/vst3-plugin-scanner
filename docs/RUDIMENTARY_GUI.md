# GUI, Inventar und Batchscan

## Zweck

`Vst3ProbeGui.exe` scannt einzelne VST3-Module oder komplette Ordner ohne
Kommandozeile. Fremder Plugin-Code wird weiterhin ausschliesslich in der separaten
`Vst3MetadataProbe.exe` geladen.

## Bedienung

1. `Vst3ProbeGui.exe` starten.
2. `VST3-Datei` oder `Scan-Ordner` waehlen. Bundle-Verzeichnisse werden im
   Ordnerscan automatisch erkannt.
3. `Pruefen` anklicken.
4. Das Inventar ueber die Spaltenkoepfe sortieren.
5. Bei Bedarf `Export CSV` oder `Export JSON` verwenden.

CSV wird mit UTF-8-BOM und Semikolon geschrieben. Dadurch laesst sich die Datei
direkt mit einer deutschen Excel-Installation oeffnen.

## Inventarmodell

- Nur Factory-Klassen der exakten Kategorie `Audio Module Class` werden als Plugins
  angezeigt.
- Name, Version, CID, SDK-Version und Kategorien bleiben unveraenderte Factory-Werte.
- Ist der Hersteller einer Klasse leer, wird der Hersteller derselben Plugin-Factory
  verwendet. Es gibt keine Namensheuristik.
- Controller-, Compatibility- und ARA-Hilfsklassen werden nicht als eigene Plugins
  gezaehlt.
- Eine CID ist nur dann eine moegliche Dublette, wenn sie aus mindestens zwei
  verschiedenen Modulpfaden stammt.
- Fehler und Dateisystemwarnungen erscheinen als eigene Problemzeilen.

## Cache

Nur wenn `Cache aktivieren` markiert ist, werden erfolgreiche Antworten unter
`<EXE-Verzeichnis>\vst3_scanner_cache.json` gespeichert. Die eine Datei enthaelt
alle gueltigen Moduleintraege. Der Schluessel beruecksichtigt:

- den kanonischen Modulpfad,
- relative Dateien eines Bundles,
- Dateigroessen,
- Aenderungszeiten.

Die Checkbox ist standardmaessig nicht markiert; ein normaler Scan prueft daher jedes
Modul neu und schreibt keine Cachedateien. Jeder Cachetreffer wird erneut als
Schema-1-JSON validiert. Fehler, Timeouts,
`no_classes` und ungueltige Protokollantworten werden nicht gespeichert. Aendert sich
ein Modul, entsteht automatisch ein neuer Cacheeintrag.

Ist das EXE-Verzeichnis bei aktiviertem Cache nicht beschreibbar, wird der Scan ohne
gespeicherte neue Eintraege fortgesetzt und die Statuszeile meldet den Schreibfehler.
Es gibt keinen stillen Fallback in ein anderes Verzeichnis.

Die Cachedatei wird pro Scan einmal geladen und am Ende atomar ueber eine temporaere
Datei ersetzt. Ist Schema oder Inhalt ungueltig, wird kein Eintrag daraus verwendet.

## Kandidatensuche

- `.vst3`-Dateien und `.vst3`-Verzeichnisse werden rekursiv gefunden.
- Ein Bundle-Verzeichnis ist genau ein Kandidat; sein Inhalt wird nicht erneut als
  Pluginpfad gescannt.
- Pfade werden kanonisiert, ohne Beachtung der Gross-/Kleinschreibung dedupliziert
  und reproduzierbar sortiert.
- Verzeichnis-Symlinks werden nicht verfolgt.
- Zugriffsfehler werden protokolliert und stoppen den Scan nicht.

## Timeout und Isolation

1. Erster Versuch: maximal 15 Sekunden.
2. Nur nach Timeout: genau eine Wiederholung mit maximal 30 Sekunden.
3. Kein dritter Versuch.

Jeder Versuch laeuft in einem eigenen Windows Job Object. Stoppen, Fensterschliessen
und Timeout beenden den gesamten Probe-Prozessbaum. Die GUI linkt kein Plugin-Hosting
und bleibt waehrend des Scans bedienbar.

## Verifizierte Tests

- Parser lehnt leere Ausgabe, defektes JSON, unbekannte Schemata und unbekannte
  Statuswerte ab.
- Nur Audio-Klassen gelangen ins Inventar.
- Factory-Hersteller wird bei leerem Klassenhersteller korrekt uebernommen.
- CID-Dubletten werden nur ueber verschiedene Modulpfade markiert.
- CSV- und JSON-Export bewahren den rohen Versionswert.
- Reales `bitcrust.vst3`: erster Lauf erzeugt den Cache; zweiter Lauf meldet einen
  Cachetreffer und startet keine erneute Probe.

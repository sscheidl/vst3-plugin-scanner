# GUI, inventory, and folder scanning

## Purpose

`Vst3ProbeGui.exe` scans individual VST3 modules or entire folders without a
command line. Third-party plug-in code is still loaded only by the separate
`Vst3MetadataProbe.exe` process.

## Usage

1. Start `Vst3ProbeGui.exe`.
2. Select `VST3 file` or `Scan folder`. Folder scans recognize bundle directories
   automatically.
3. Click `Scan`.
4. Sort the inventory by clicking a column header.
5. Use `Export CSV` or `Export JSON` when needed.

CSV uses a UTF-8 BOM and semicolon separators for direct spreadsheet import.
Formula-like values from untrusted plug-in metadata are prefixed safely.

## Inventory model

- Only factory classes with the exact `Audio Module Class` category are shown as
  plug-ins.
- Name, version, CID, SDK version, and categories remain unmodified factory values.
- `Version source` shows `VST3 factory` when the factory supplies a non-empty raw
  version; otherwise it shows `Not reported`.
- An empty class vendor falls back only to the vendor of the same plug-in factory.
  No name heuristic is used.
- Controller, compatibility, and ARA helper classes are not counted as plug-ins.
- Multi-plug-in modules such as WaveShells are not represented by one shell row.
  Every reported audio class appears as a separate plug-in while `Module` shows
  the shared WaveShell file.
- A CID is a possible duplicate only if it appears under at least two different
  module paths.
- CID remains available internally and in JSON, but is hidden from the GUI and CSV.
- Errors and file-system warnings appear as separate issue rows.

## Cache

Successful responses are stored in
`<executable-directory>\vst3_scanner_cache.json` only when `Enable cache` is
selected. This single file contains all valid module entries. Its key includes:

- the canonical module path;
- relative files within a bundle;
- file sizes;
- modification times;
- the full content of VST3/DLL binaries and `moduleinfo.json`.

The checkbox is cleared by default. A normal scan therefore probes every module
again and writes no cache file. Every cache hit is revalidated as schema-2 JSON.
Errors, timeouts, `no_classes`, and invalid protocol responses are never stored.
Changing a module automatically creates a new cache entry.

If the executable directory is not writable, scanning continues without saving
new entries and the status line reports the write failure. There is no silent
fallback to a different directory.

The cache is loaded once per scan and replaced atomically through a temporary
file at the end. An invalid schema or payload causes the entire cache to be ignored.

## Candidate discovery

- `.vst3` files and `.vst3` directories are found recursively.
- A bundle directory is one candidate; its contents are not scanned again as
  separate plug-in paths.
- Paths are canonicalized, deduplicated case-insensitively, and sorted
  deterministically.
- Directory symlinks are not followed.
- Access errors are reported but do not stop the scan.

## Timeout and isolation

1. Initial attempt: up to 15 seconds.
2. On timeout only: exactly one retry with a 30-second limit.
3. No third attempt.

Each attempt runs in its own Windows Job Object. Stop, window close, and timeout
terminate the entire probe process tree. The GUI links no plug-in-hosting code
and remains responsive during a scan. If termination fails, the scanner waits no
more than five additional seconds and then produces a visible issue row instead
of waiting indefinitely for pipe-reader threads.

## Verified tests

- The parser rejects empty output, malformed JSON, unknown schemas, and unknown
  status values.
- Only audio classes enter the inventory.
- An empty class vendor correctly falls back to the factory vendor.
- CID duplicates are marked only across different module paths.
- A simulated WaveShell response creates one inventory row per audio class.
- Inconsistent `versionMissing` values, class counts, and class indices are
  rejected as protocol errors.
- Duplicate audio CIDs in a `partial` module are reduced to one inventory row;
  they are protocol errors in an `ok` response.
- CSV and JSON preserve the raw version value.
- A real `bitcrust.vst3` run creates the cache on the first pass; the second pass
  reports a cache hit and does not start another probe.

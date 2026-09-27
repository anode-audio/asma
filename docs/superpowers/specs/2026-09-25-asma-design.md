# asma: design spec

Date: 2026-09-25. Status: draft for review.

asma (Anode labs Sample MAnager) is a free, open-source sample manager for
Windows, macOS and Linux. It runs as a standalone app and as a plugin, indexes
local sample libraries, analyses them with classic DSP, and lets you audition
samples in the context of your project (tempo, key, MIDI) before dragging them
into the DAW.

## 1. Goals

Three drivers, weighted equally:

- **Brand funnel.** A polished free tool that puts Anode Labs in front of
  producers and points them at the paid plugins.
- **Own use.** Good enough that we use it daily, including on Linux and for the
  MPC Sample workflow.
- **Community.** Architecture and tooling that outside contributors can pick up:
  plain CMake, a headless core with a CLI, deterministic tests.

### Positioning

Inspired by ADSR Sample Manager (standalone + VST/AU, auto-tagging, tempo/key
sync, MIDI audition, find-similar, drag to DAW; Mac/Windows only). Its public
reviews point at the gaps asma targets: crashes during scans and bulk edits,
slow scanning (16 GB in 30 minutes), laggy search, heavy DAW CPU, unreliable
genre/instrument tags, and a "free" product that costs money.

The only notable open-source alternative, Pulp (vincehi/pulp), is a desktop-only
Tauri app with no plugin, no tempo/key sync and no MIDI, last released
June 2024. asma's differentiators: plugin plus in-context audition, Linux
support, speed, and crash isolation.

### Success criteria

- Scans a 16 GB library in under 5 minutes on an SSD.
- A search keystroke returns results in under 50 ms with 200k indexed samples.
- The plugin never scans or writes to the database inside the host process.
- No file operation can lose data: every operation is journaled and undoable,
  and deletes go to the OS trash only.

## 2. Constraints

- **License:** GPLv3 for asma's own code. JUCE 8 is used under its AGPLv3
  option, which is compatible.
- **Repo:** `anode-audio/asma` on GitHub, standalone. It does **not** depend on
  `anode-common` (which is proprietary). The Anode look is reproduced by copying
  palette and theme values into asma's own `Theme`, not by sharing code.
- **Platforms:** macOS 12+ (universal binary), Windows x64, Linux x64 (Ubuntu
  22.04 baseline). Linux ARM64 is a later addition.
- **Plugin formats:** VST3, AU (macOS), CLAP, LV2 (Linux).
- **No accounts, no cloud, no telemetry.**

## 3. Scope

### In v1

- **Library:** user-added root folders, incremental scans (mtime + size +
  content hash), a file watcher while the app runs, and re-linking of files
  moved outside the app by hash match.
- **Analysis (DSP only):** loop vs one-shot, BPM, key, duration, peak, LUFS,
  spectral descriptors. Embedded metadata (ACID chunk, `smpl` chunk) and BPM/key
  parsed from filenames. Instrument tags guessed from filename and folder tokens
  (kick, snare, pad, 808, ...).
- **Search:** instant FTS5 text search; facets on type, BPM range, key,
  instrument, duration, format, rating; find-similar via nearest neighbours on a
  feature vector.
- **Organise:** user tags, favourites, 1 to 5 ratings, collections (virtual
  folders), saved searches.
- **Audition:** click to play, auto-play while navigating, tempo sync for loops,
  transpose to key, MIDI playback, start/end trim, reverse and ping-pong,
  waveform display.
- **Drag out** to DAW or file manager, rendering edits to a temp WAV when any
  edit is active.
- **File manager (standalone only):** rename (including batch rename by
  pattern), move, trash, convert format and sample rate, find duplicates by
  hash, export a collection to a folder. Every operation is journaled and
  undoable.
- **Tempo source:** host transport in the plugin; Ableton Link or a manual BPM
  in the standalone.
- **Formats:** read WAV, AIFF, FLAC, OGG, MP3; write WAV, AIFF, FLAC.

### Out of v1

Cloud stores and accounts, ML tagging or embeddings, slicing loops to MIDI,
editing beyond trim and reverse, REX and other proprietary formats.

## 4. Architecture

Approach: a headless core library, an out-of-process scanner, and a shared
SQLite database read directly by every UI instance. No long-lived daemon.

```
            +------------------+        spawns        +-----------+
            |  asma standalone |--------------------->|  asma-scan|
            |  (file ops ON)   |<-- JSON-lines stdout-|  (worker) |
            +--------+---------+                      +-----+-----+
                     |  read + journaled writes             | writes
                     v                                      v
                 +------------------------------------------------+
                 |        SQLite (WAL) in the user data dir       |
                 +------------------------------------------------+
                     ^  read only
            +--------+---------+
            | asma plugin (xN) |   (browse, audition, drag; no file ops)
            +------------------+
```

### Components

1. **`asma-core`** (static library, no GUI dependencies). Modules:
   - `db`: schema, migrations, connection setup (WAL, busy timeout).
   - `index`: filesystem walk, diff against the `files` table, re-link logic.
   - `analysis`: pure functions from an audio buffer to features. No I/O.
   - `query`: translates a search model (text + facets + sort) into SQL.
   - `fileops`: planning, preflight, journal, execute, undo.
2. **`asma-scan`**: worker executable. Wraps `asma-core` `index` and `analysis`,
   reports progress as JSON lines on stdout.
3. **`asma` CLI**: `scan`, `query`, `similar`, `dedupe`, `undo`. Used by CI and
   power users.
4. **`asma-ui`** (JUCE component library): browser table, sidebar, filter bar,
   waveform, audition engine, theme. Shared by both shells.
5. **Standalone and plugin shells**: thin wrappers around `asma-ui`. Only the
   standalone enables file operations and spawns the scanner. If the plugin is
   the only asma instance running, it can request a scan by launching
   `asma-scan`, which still runs outside the host process.

Each unit is testable without the ones above it: `analysis` with synthetic
buffers, `index` and `fileops` with temp directories, `query` against an
in-memory database, the CLI end to end.

## 5. Data model

One SQLite database per user in the platform data directory
(`~/Library/Application Support/Anode Labs/asma/`, `%APPDATA%\Anode Labs\asma\`,
`$XDG_DATA_HOME/anode-labs/asma/`). WAL mode, versioned migrations.

| Table                             | Contents                                                                                                                                                                                                                                               |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `roots`                           | id, absolute path, enabled                                                                                                                                                                                                                             |
| `files`                           | id, root_id, relative path, size, mtime, content_hash (xxh3 over the audio data chunk only, so metadata edits keep the hash), format, sample rate, channels, bit depth, duration, status (`ok`, `missing`, `failed`), failure reason, analysis_version |
| `features`                        | file_id, bpm, bpm_confidence, key, key_confidence, is_loop, peak, lufs, spectral centroid, rolloff, flatness, onset density, feature_vector (float32 blob)                                                                                             |
| `tags`, `file_tags`               | tag name; file_id, tag_id, source (`auto`, `embedded`, `user`)                                                                                                                                                                                         |
| `ratings`, `favourites`           | per file                                                                                                                                                                                                                                               |
| `collections`, `collection_items` | virtual folders                                                                                                                                                                                                                                        |
| `saved_searches`                  | serialised search model                                                                                                                                                                                                                                |
| `fts_files`                       | FTS5 over filename, folder path, tags                                                                                                                                                                                                                  |
| `journal`                         | id, group_id, op, src, dst, trash_ref, state (`planned`, `done`, `undone`), timestamp                                                                                                                                                                  |

Rules:

- `user` tags win over `auto` and `embedded` ones; re-analysis never touches
  `user` rows.
- Missing files keep their row (status `missing`) so remounting a drive restores
  them with all user data.
- Bumping `analysis_version` triggers lazy background re-analysis of affected
  rows.

## 6. Data flow

### Scan

1. The UI triggers a scan (root added, watcher event, manual rescan) and spawns
   `asma-scan --db <path> --root <id>`.
2. The worker walks the root and diffs against `files`:
   - new path: queue for analysis;
   - mtime or size changed: re-hash; re-analyse only if the hash changed;
   - path gone but hash present elsewhere: re-link the row (tags, ratings and
     collections follow);
   - path gone, no match: mark `missing`.
3. Analysis runs on a thread pool, one file per task. Results are committed in
   transactions of 200 files.
4. Progress goes to stdout as JSON lines, for example
   `{"done":1200,"total":8000,"current":"Drums/kick_01.wav"}`.
5. If the worker crashes, the UI marks the file named in the last progress line
   as `failed` and respawns the worker, which skips files already `failed` at
   the same hash.
6. Readers notice changes via SQLite `PRAGMA data_version` polling and refresh
   the visible result set incrementally.

### Single writer

Only `asma-scan` and standalone file operations write. A lock file in the data
directory holds the writer's PID; a stale lock (dead PID) is taken over. File
operations wait for a running scan to finish, or pause it.

### File operations

1. **Plan:** build the full list of source and destination pairs.
2. **Preflight:** check permissions, free space and name conflicts for every
   item; show a preview; nothing touches disk if any check fails.
3. **Journal:** write all rows as `planned` under one `group_id`.
4. **Execute:** perform each operation on disk, then mark it `done` and update
   `files` in the same transaction.
5. **Undo:** Ctrl/Cmd+Z reverses the most recent group in reverse order (trash
   items are restored from the OS trash).
6. **Recovery:** on startup, any group with `planned` rows is completed or
   rolled back so disk and database agree.

Delete always means move to OS trash. If the trash is unavailable (some Linux
setups, network volumes) the delete is refused with a clear message; there is no
permanent-delete fallback.

## 7. Analysis

All in-house, kept small and deterministic:

- **Loop vs one-shot:** duration, onset pattern regularity, and whether the
  length fits a whole number of bars at the estimated tempo.
- **BPM:** onset-strength envelope plus autocorrelation, with octave-error
  correction; confidence from peak prominence. For a complete file, only the
  tempos that fit a whole number of bars are considered. Embedded ACID tempo
  and filename BPM override when present.
- **Key:** chroma profile correlated against major/minor key templates;
  confidence from the margin between best and second-best key. Only reported for
  tonal material (low spectral flatness).
- **Loudness:** LUFS via libebur128; sample peak.
- **Descriptors and similarity:** spectral centroid, rolloff, flatness, onset
  density, and a small MFCC-summary vector. Find-similar is brute-force cosine
  distance over normalised vectors (fast enough at 200k rows; an index can come
  later if measurements say so).
- **Instrument tags:** token dictionary over filename and folder path, shipped
  as a data file contributors can extend.

We don't use Essentia: it's AGPL and a heavy build, which works against
contributor ergonomics.

## 8. Audition engine

- **Loading:** files under ~10 s are decoded fully; longer files stream with
  read-ahead. An LRU cache keeps recent previews warm. Decoding runs off the
  audio thread; the audio thread only swaps in ready buffers, lock-free.
- **Chain:** trim, direction (forward, reverse, ping-pong), time-stretch and
  pitch-shift (Signalsmith Stretch), LUFS-based gain matching, short anti-click
  fades.
- **Sync:** loops stretch to the current tempo and start on the next beat or bar
  (optional quantised start). Transpose-to-key takes the shortest interval. Sync
  and transpose only apply above a confidence threshold; below it the sample
  plays unmodified and shows a "?" badge.
- **MIDI:** incoming notes play the selected sample pitched from a root note
  (detected pitch if available, else C3). 8-voice polyphony, simple AR envelope.
- **Plugin output:** the preview is rendered into the plugin's audio output, so
  it's heard through the channel's inserts. By default the plugin stays silent
  while the host transport is stopped unless the user is auditioning.

## 9. UI

Native JUCE, dark theme by default, palette values copied from the Anode
identity.

- **Left sidebar:** roots, collections, saved searches, favourites.
- **Top bar:** search field, facet chips (type, BPM range, key, instrument,
  duration, rating), sync toggles.
- **Centre:** virtualised `TableListBox` (name, type, BPM, key, duration,
  rating, tags); sortable, resizable, fully keyboard driven.
- **Bottom panel:** waveform with trim handles and playhead, preview controls,
  and a "Similar" strip showing the 10 nearest neighbours.
- **File operations:** context menu and batch dialogs, always with a preview;
  Ctrl/Cmd+Z undoes.
- **Problems panel:** files that failed to decode or analyse, with the reason
  and a retry action.

### Drag out

- No edits active: drag the original file path.
- Edits active (trim, reverse, stretch, pitch): render to
  `<cache>/renders/<hash>-<params>.wav` at the host sample rate if known, then
  drag that file. The render cache is capped (default 2 GB, LRU eviction).

### Plugin state

Instrument plugin with MIDI input and audio output. Any number of instances
share the one database. The project saves only the selected sample, the search
model and view state, never the library itself.

## 10. Error handling

- **Decode or analysis failure:** file marked `failed` with a reason, shown in
  the Problems panel; never blocks the scan.
- **Scanner crash:** handled as in section 6, step 5.
- **Database corruption:** `PRAGMA integrity_check` at startup; on failure the
  file is moved aside as `.corrupt`, a new database is created and a rescan
  starts. User data (tags, ratings, collections, favourites, saved searches) is
  exported nightly to a JSON sidecar next to the database and re-imported,
  matched by content hash.
- **File operation failure mid-group:** execution stops, the group is left
  partially `done`, and the user is offered undo of the completed part.
- **Trash unavailable:** delete refused, no fallback.

## 11. Build and dependencies

CMake + Ninja. Dependencies fetched with CPM/FetchContent at pinned versions:

| Dependency                        | Use                              | License               |
| --------------------------------- | -------------------------------- | --------------------- |
| JUCE 8                            | UI, audio, plugin formats        | AGPLv3                |
| clap-juce-extensions              | CLAP target                      | MIT                   |
| SQLite (with FTS5)                | database                         | public domain         |
| xxHash                            | content hashing                  | BSD-2                 |
| Signalsmith Stretch               | time-stretch, pitch-shift        | MIT                   |
| dr_libs (dr_flac, dr_mp3, dr_wav) | decoding in asma-core            | MIT-0 / public domain |
| stb_vorbis                        | Ogg Vorbis decoding in asma-core | MIT / public domain   |
| libebur128                        | LUFS                             | MIT                   |
| Ableton Link                      | standalone tempo sync            | GPLv2+                |
| Catch2                            | tests                            | BSL-1.0               |

asma-core decodes audio with dr_libs and stb_vorbis rather than JUCE, so the
core, the CLI and the scanner build without JUCE.

## 12. Testing

- **Unit tests (`asma-core`):** scan diffing (new, changed, moved, missing),
  query-to-SQL translation, migrations, and journal recovery with a simulated
  crash injected at every step of plan, journal, execute.
- **Analysis accuracy corpus:** generated fixtures (drum loops at known BPMs,
  chord progressions in known keys, loops vs one-shots), built at test time so
  no third-party audio is stored. CI fails if BPM or key accuracy drops below
  the recorded baseline. Real-world accuracy is measured by an opt-in test
  against the BPM and key a user's own library already states in file names
  and ACID chunks; nothing from that library is copied or uploaded.
- **CLI end to end:** `asma scan` and `asma query` over a fixture tree,
  asserting results.
- **Plugin validation:** pluginval (VST3, AU) and clap-validator (CLAP).
- **Performance:** scan and query timings over a synthetic 50k-file library,
  reported in CI (informational, not gating).
- **CI:** GitHub Actions on macOS, Windows and Linux for every push and PR.
  Release builds are dispatch-only.

## 13. Distribution

- GitHub Releases: macOS `.pkg`, Windows Inno Setup installer, Linux `.tar.gz`
  with a static executable + plugin plus an AppImage for the standalone.
- Listed on the Anode Labs website, no Gumroad.
- Code signing is for macos only, windows costs too much and Linux doesn't need
  any.

## 14. Open items outside this spec

- Trademark check for "ASMA" in Nice class 9 on TMview (EUIPO and national EU
  offices) and USPTO before public launch. A web search found no conflicting
  audio or music software.
- Create `anode-audio/asma` on GitHub, then register it in the umbrella
  `scripts/repos.sh` and the product list in the umbrella `CLAUDE.md`.

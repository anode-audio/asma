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

- **License:** GPLv3 for asma's own code. JUCE 9 is used under its AGPLv3
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
- **Drag out** to DAW or file manager, rendering edits to a WAV when any edit is
  active.
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
3. **`asma` CLI**: `scan`, `query`, `similar`, `render`, `dedupe`, `undo`. Used
   by CI and power users.
4. **`asma-audio`** (static library, no JUCE): the audition engine of section 8
   and the drag-out renders. Built on `asma-core`'s decoders and Signalsmith
   Stretch, so it runs and tests headless; the plugin's audio callback calls it
   directly.
5. **`asma-ui`** (`plugin/`, JUCE): the processor, the editor and the parts they
   share (library view, browser, plugin state, scan job). An INTERFACE library,
   so the plugin and its headless tests each compile it.
6. **Standalone and plugin shells**: one JUCE plugin target builds VST3, AU
   (macOS), CLAP (through clap-juce-extensions), LV2 (Linux) and the Standalone;
   the processor tells the standalone from a plugin at run time. Only the
   standalone adds folders and spawns the scanner, which it ships beside its own
   executable. If the plugin is the only asma instance running, it can request a
   scan by launching `asma-scan`, which still runs outside the host process.

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

Only `asma-scan` and standalone file operations take the writer lock. A lock
file in the data directory holds the writer's PID; a stale lock (dead PID) is
taken over. File operations wait for a running scan to finish, or pause it.

User data (ratings, favourites, user tags, collections, saved searches) is
written in short transactions without the writer lock, so rating a sample never
waits for a scan to end; SQLite's busy timeout covers the moment a scan batch is
committing. The plugin never writes inside the host: it runs the `asma` CLI
(`asma rate`, `asma fav`, `asma tag`, `asma collection`, `asma search`) as a
short-lived helper process, and opens the library read-only itself.

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
  tempos that fit a whole number of bars are considered. Embedded ACID tempo and
  filename BPM override when present.
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

- **Loading:** files up to 10 s are decoded fully; longer files stream in
  blocks, with the first 10 s and the last block loaded up front so playback
  starts at once in either direction, and the rest read ahead in the direction
  of travel. An LRU cache (256 MB of samples) keeps recent previews warm.
  Decoding runs on a loader thread; the audio thread only takes ready previews
  from a lock-free queue, and a preview is freed only once the audio thread
  reports it no longer plays it (as the selection, the sound fading out, or a
  ringing MIDI voice). The audio thread never allocates.
- **Overview:** the loader also builds the waveform the UI draws: a min/max pair
  per channel at 2,048 points over the whole file. A fully decoded file gets it
  from its buffer at once; a streamed one gets it from a read-through on the
  loader thread after playback has started, and the UI draws it when it is
  complete. Nothing is stored in the library.
- **Chain:** trim, direction (forward, reverse, ping-pong), resampling to the
  output rate, time-stretch and pitch-shift (Signalsmith Stretch, pre-rolled so
  the first frame still comes out first), LUFS-based gain matching (to -16 LUFS,
  at most +12 dB, never boosting the peak past -1 dBFS; files not analysed yet
  are measured on load when short, and play at unity when streamed), and 5 ms
  anti-click fades. Fades go only where a sound would click: not at a forward
  start from frame 0, a one-shot's own end, or the wrap of an untrimmed forward
  loop. An edit while a sample plays restarts it; switching samples fades the
  old one out first.
- **Sync:** loops stretch to exactly the current tempo, however far that is from
  the original (within the stretcher's 0.25x to 4x), and can start on the next
  beat or bar while the transport plays (optional quantised start). A synced
  loop follows tempo changes as it plays. Transpose-to-key takes the shortest
  interval (-6..+5 semitones), to the project's relative key when the modes
  differ. Sync and transpose only apply at a tempo confidence of 0.3 or a key
  confidence of 0.7 and above (file names and embedded chunks always qualify);
  below it the sample plays unmodified and shows a "?" badge. The plan says when
  the ratio was clamped to 0.25x or 4x, so the UI can say so. The thresholds
  were measured on labelled libraries. Key sync is off by default: on guitar
  recordings key detection was unreliable at every confidence.
- **MIDI:** incoming notes play the selected sample pitched from a root note
  (the `smpl` chunk's, else MIDI note 60, which most DAWs call C3), like a
  classic sampler: speed changes with pitch. Each note plays the trimmed region
  once in the chosen direction. 8-voice polyphony, linear AR envelope (2 ms, 80
  ms); a new note beyond eight takes a releasing voice first, then the oldest.
- **Plugin output:** the preview is rendered into the plugin's audio output, so
  it's heard through the channel's inserts. By default the plugin stays silent
  while the host transport is stopped unless the user is auditioning. The host's
  tempo, position and play state reach the engine every block; blocks are split
  at MIDI events so notes and quantised starts land on their own sample.
- **Standalone tempo:** Ableton Link when it is on (tempo, beat and start/stop
  from the session), otherwise the manual tempo.

## 9. UI

Native JUCE, dark theme by default, palette values copied from the Anode
identity. Plan 3c1 ships a plain browser with the behaviour below that already
works (search, audition as the selection moves, sync switches, a status line
with the "?" badge, drag-out, and in the standalone the tempo source and "Add
folder"). Plan 3c2a builds the layout, the looks and the audition controls; plan
3c2b fills in the organising parts (sidebar, facet chips, ratings, tags and
favourites, the Similar strip, the Problems panel).

The approved design (a design canvas, private to the author) is recorded in the
repository as `tests/ui/reference/main.png`: its main artboard with the 3c2b
areas blanked. The editor is built to match it.

- **Left sidebar (3c2b):** All samples, Favourites, folders (roots),
  collections, saved searches, and a Problems entry with a count at the foot.
- **Top bar:** the asma mark, the search field with a result count, and on the
  right the tempo source: in the standalone the Link switch, the BPM box and
  "Add folder…"; in a plugin the host's tempo, read-only ("host 124.0 BPM").
- **Chip row (3c2b):** facet chips (type, BPM range, key, instrument, duration,
  rating) and "Save search", between the top bar and the table.
- **Centre:** virtualised `TableListBox` (favourite, name, type, BPM, key,
  length, rating, tags); sortable, resizable, fully keyboard driven. When there
  is no library, or it is outdated or unreadable, the table area says so (with
  "Add folder…" in the standalone) instead of a status line.
- **Bottom panel:** the selected file's name and format line; the waveform with
  trim handles, the trimmed-off parts dimmed and a playhead; and the preview
  controls: play/stop, a direction switch (forward, reverse, ping-pong), loop
  mode (auto, on, off), "Reset edits", and chips for Tempo, Key (with the
  project key), Match loudness and Start (quantise). The "Similar" list (3c2b)
  takes the panel's right side, the 10 nearest neighbours.
- **Footer:** what a drag-out carries ("the original file", or e.g. "reversed,
  trimmed, stretched to 180 BPM"), the scan's progress and outcome, the kept
  renders' size and "Clear renders".
- **File operations (plan 4):** context menu and batch dialogs, always with a
  preview; Ctrl/Cmd+Z undoes.
- **Problems panel (3c2b):** files that failed to decode or analyse, with the
  reason and a retry action.

### Look

`Theme` holds the Anode values: grounds `#0b0c0e`, `#111316`, `#15171a`, raised
`#1c1f23`, borders `#2a2d33`, text `#e8e9eb` and muted `#8a8f98`; filament amber
`#e8a33d` only for what is active or selected (the selected row's bar, trim
handles, pressed switches); oscilloscope cyan `#4fd1e6` for the waveform; signal
green `#7de38e` only for a synced tempo. Space Grotesk for headings and section
labels, Inter for text, JetBrains Mono for numbers, all three embedded (SIL
OFL). 4 px corners, 8 px on cards. A custom `LookAndFeel` and a few components
(waveform, segmented switch, chip) carry it; there is no web view.

### Tempo chip

The chip says what tempo sync does to the selected sample, and why when it does
nothing:

| Case                    | Chip                       | Colour |
| ----------------------- | -------------------------- | ------ |
| loop, synced            | `120 → 180 · x1.50`        | green  |
| loop, at 0.25x or 4x    | `120 → 30 · x0.25 max`     | amber  |
| loop, tempo below 0.3   | `~97 ? · plays as is`      | amber  |
| loop, no tempo known    | `? · plays as is`          | amber  |
| no tempo from the host  | `no tempo · plays as is`   | muted  |
| one-shot                | `one-shot · plays as is`   | muted  |
| no loop verdict (stems) | `not a loop · plays as is` | muted  |
| tempo sync switched off | `off`                      | muted  |
| file cannot be read     | `can't read this file`     | amber  |

### Editing in the preview

- Edits (trim, direction, loop mode) belong to the selected sample: selecting
  another sample resets them, so moving through the list always plays each
  sample as it is. The project saves the selected sample's edits and restores
  them with it. Changing an edit restarts the sound.
- Trim handles drag the start and end, at least 10 ms apart; double-clicking a
  handle resets it. Clicking the waveform plays from the trimmed start; there is
  no click-to-seek.
- Space plays and stops; the arrow keys and Return work as in 3c1.

### Window

Resizable, 1100×720 by default and at least 900×600; the size is saved with the
project. The table takes the extra room; the top bar and bottom panel keep their
height.

### Drag out

- No edits active: drag the original file path. A sample-rate difference alone
  is not an edit; the DAW converts on import.
- Edits active (trim, reverse, stretch, pitch): render one pass to
  `<data dir>/renders/<hash>-<params>.wav` (32-bit float, the file's channels)
  at the host sample rate if known, then drag that file. The length is exact,
  the trimmed pass divided by the tempo ratio, so a synced loop lands on the
  grid.
- Renders are kept until the user clears them (`asma renders clear`, and a
  "Clear renders" action in the UI), never evicted automatically: DAWs that play
  a dragged file where it lies (Reaper, and Live unless the set is collected)
  would lose audio. They live in the data directory rather than a cache
  directory so that cleaner apps leave them alone. (macOS itself does not purge
  `~/Library/Caches`; only apps and cleaners do.)

### Plugin state

Instrument plugin with MIDI input and audio output. Any number of instances
share the one database and one preview cache. The project saves only the
selected sample, the search model (as the same JSON a saved search uses) and
view state (sync switches, project key, gain matching, quantise, edits, the
standalone's tempo source, window size), never the library itself. State is one
versioned JSON object; unknown fields and bad values are skipped, so a project
from another asma version loads what it can. A restored selection is loaded
without playing, with its tempo and key from the library.

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
- **Loader failure:** anything a preview load throws, not only a probe error,
  marks that preview failed ("can't read this file" in the Tempo chip); the
  loader thread carries on with the next selection.

## 11. Build and dependencies

CMake + Ninja. Dependencies fetched with CPM/FetchContent at pinned versions:

| Dependency                        | Use                              | License               |
| --------------------------------- | -------------------------------- | --------------------- |
| JUCE 9                            | UI, audio, plugin formats        | AGPLv3                |
| clap-juce-extensions              | CLAP target                      | MIT                   |
| SQLite (with FTS5)                | database                         | public domain         |
| xxHash                            | content hashing                  | BSD-2                 |
| Signalsmith Stretch (and Linear)  | time-stretch, pitch-shift        | MIT                   |
| dr_libs (dr_flac, dr_mp3, dr_wav) | decoding in asma-core            | MIT-0 / public domain |
| stb_vorbis                        | Ogg Vorbis decoding in asma-core | MIT / public domain   |
| libebur128                        | LUFS                             | MIT                   |
| Ableton Link                      | standalone tempo sync            | GPLv2+                |
| Catch2                            | tests                            | BSL-1.0               |

asma-core decodes audio with dr_libs and stb_vorbis rather than JUCE, so the
core, the audio engine, the CLI and the scanner build without JUCE.

## 12. Testing

- **Unit tests (`asma-core`):** scan diffing (new, changed, moved, missing),
  query-to-SQL translation, migrations, and journal recovery with a simulated
  crash injected at every step of plan, journal, execute.
- **Analysis accuracy corpus:** generated fixtures (drum loops at known BPMs,
  chord progressions in known keys, loops vs one-shots), built at test time so
  no third-party audio is stored. CI fails if BPM or key accuracy drops below
  the recorded baseline. Real-world accuracy is measured by an opt-in test
  against the BPM and key a user's own library already states in file names and
  ACID chunks; nothing from that library is copied or uploaded.
- **CLI end to end:** `asma scan` and `asma query` over a fixture tree,
  asserting results.
- **Plugin tests:** the processor and editor run headless in a JUCE console app
  (MIDI timing, host transport, state, the browser, drag-out, scanning).
- **UI fidelity:** a headless test renders the editor at 1280×800 with fixed
  demo data and compares it with `tests/ui/reference/main.png` (the approved
  design, 3c2b areas blanked), failing above a set mismatch. It runs on macOS
  only, since font rendering differs between systems; the behaviour tests run
  everywhere.
- **Plugin validation:** pluginval at strictness 10 (VST3 everywhere, AU on
  macOS) and clap-validator (CLAP) in CI on all three platforms.
  clap-validator's `param-conversions` test is skipped: it divides by the
  parameter count, and asma has no parameters.
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

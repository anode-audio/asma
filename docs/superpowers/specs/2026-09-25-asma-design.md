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
  content hash), a file watcher while any asma window is open, and re-linking of
  files moved outside the app by hash match.
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

- **After plan 4:** following YouTube playlists through the system's yt-dlp, and
  processing new tracks into stems and drum one-shots (section 15).

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

1. A scan starts when a folder is added, when the watcher reports a change, once
   per folder at startup and every 15 minutes (see Watching); it spawns
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

### Watching

The app and the plugins keep the library in step with the sample folders while
any asma window is open, so a sample dropped into a folder appears in seconds
and one deleted or renamed outside asma goes or follows.

- **`FolderWatcher` (core, no JUCE)** watches each enabled folder recursively
  through the operating system's change notices (FSEvents,
  ReadDirectoryChangesW, inotify, through efsw), and reports a folder changed
  once it has been quiet for 2 seconds: a burst of files gives one report.
  Hidden files and asma's own data directory never count.
- **`LibraryKeeper`**, one per process (shared by the app, or by every asma
  instance in one host), reads the folders from the library and again whenever
  the library changes, so a folder added elsewhere is watched here too. It scans
  each enabled folder once at startup, a folder the watcher reports, and every
  folder every 15 minutes (for what the notices miss: network shares, some
  external drives, inotify's watch limit). Scans run through `asma-scan`, one at
  a time; a folder already queued is not queued twice.
- **Across processes the writer lock decides**: a scan refused by it is skipped,
  and the next change or poll catches up. Nothing waits or spins.
- **A missing folder** (an unplugged drive) is not watched; the poll notices it
  back, rescans it and watches it again.
- **Plugins scan too**, through `asma-scan` shipped beside their binary as
  `asma-cli` is; the plugin still never writes the library inside the host. The
  footer shows a scan's progress in a plugin as in the app.

### Single writer

Only `asma-scan`, `asma retry`, `asma repair` and standalone file operations
take the writer lock. A lock file in the data directory holds the writer's PID;
a stale lock (dead PID) is taken over. File operations wait for a running scan
to finish, or pause it.

User data (ratings, favourites, user tags, collections, saved searches) is
written in short transactions without the writer lock, so rating a sample never
waits for a scan to end; SQLite's busy timeout covers the moment a scan batch is
committing. The plugin never writes inside the host: it runs the `asma` CLI
(`asma rate`, `asma fav`, `asma tag`, `asma collection`, `asma search`,
`asma retry`) as a short-lived helper process, and opens the library read-only
itself. Every format ships the CLI beside its own binary as `asma-cli` (the
app's and the bundles' binaries are themselves called `asma`). `asma retry` is
the one of these that takes the writer lock: it re-reads and re-analyses the
files it is given, as a scan would.

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
folder"). Plan 3c2a builds the layout, the looks and the audition controls. Plan
3c2b1 fills in browsing (sidebar, filter chips, the full table, the Similar
list), reading the library only; plan 3c2b2 adds what writes it (ratings,
favourites, tags, collections, saved searches, the Problems panel).

The approved design (a design canvas, private to the author) is recorded in the
repository as `tests/ui/reference/main.png`: its main artboard filled with the
test's demo library. The editor is built to match it. The canvas also holds the
filter chips' popovers (approved for 3c2b1) and the organising actions and
Problems panel (approved for 3c2b2).

- **Left sidebar (3c2b1):** All samples, Favourites, folders (roots),
  collections and saved searches, with counts; a Problems entry with a count at
  the foot while any file has failed to decode or analyse.
- **Top bar:** the asma mark, the search field with a result count, and on the
  right the tempo source: in the standalone the Link switch, the BPM box and
  "Add folder…"; in a plugin the host's tempo, read-only ("host 124.0 BPM").
- **Chip row (3c2b1):** filter chips (type, BPM range, key, instrument, length,
  rating) and "Clear all", between the top bar and the table, and "Save search"
  (3c2b2) at its right end.
- **Centre:** virtualised `TableListBox` (favourite, name, type, BPM, key,
  length, rating, tags); sortable, resizable, fully keyboard driven. Lengths
  under a minute read in seconds ("7.38 s"), longer ones as a clock ("5:01",
  "1:02:05"), here and in the preview's file line. When there is no library, or
  it is outdated or unreadable, the table area says so (with "Add folder…" in
  the standalone) instead of a status line.
- **Bottom panel:** the selected file's name and format line; the waveform with
  trim handles, the trimmed-off parts dimmed and a playhead; and the preview
  controls: play/stop, a direction switch (forward, reverse, ping-pong), loop
  mode (auto, on, off), "Reset edits", and chips for Tempo, Key (with the
  project key), Match loudness and Start (quantise). The "Similar" list (3c2b1)
  takes the panel's right side, the 10 nearest neighbours.
- **Footer:** what a drag-out carries ("the original file", or e.g. "reversed,
  trimmed, stretched to 180 BPM"), the scan's progress and outcome, the kept
  renders' size and "Clear renders".
- **File operations (plan 4):** context menu and batch dialogs, always with a
  preview; Ctrl/Cmd+Z undoes.
- **Problems panel (3c2b2):** files that failed to decode or analyse, with the
  reason and a retry action, in place of the table.

### Browsing (3c2b1)

- **The sidebar sets the scope, the chips narrow it.** All samples, Favourites,
  a folder or a collection sets where the search looks; picking one clears the
  others (the search model holds one scope). The text and the chips narrow
  within it. Counts come from the library and refresh when it changes.
- **A saved search** loads its whole search, text and chips included, and stays
  highlighted until something changes.
- **Filter chips** open a popover anchored under the chip; changes apply as they
  are made, and the whole search is saved with the project:

  | Chip       | Popover                                                         | Active chip says |
  | ---------- | --------------------------------------------------------------- | ---------------- |
  | Type       | any / loops / one-shots switch                                  | `Loops`          |
  | BPM        | from and to; "near 120" and "near the tempo"                    | `118–132 BPM`    |
  | Key        | the 24 keys, any of                                             | `Am, C`          |
  | Instrument | the library's tags, most used first, all of                     | `bass, synth`    |
  | Length     | presets (under 1 s, 1–10 s, 10–60 s, over a minute) and from/to | `1–10 s`         |
  | Rating     | a minimum in stars; unrated never match                         | `★★★ and up`     |

  An active chip is amber with a × that clears it; "Clear all" clears every
  chip. Samples with no loop verdict show only under Any.

- **The table** shows every match: its rows are as many as the search matches,
  fetched in pages of 500 as the view scrolls, so the scrollbar is true and the
  top bar's count is not a cap. Clicking a header sorts by name, BPM, length,
  key or rating (type, tags and the favourite star do not sort); the sort is
  saved with the project. The favourite, rating and tags columns show what the
  library holds; changing them is 3c2b2 (below).
- **Similar** lists the 10 samples nearest the selection by sound, with the
  distance. Clicking one auditions it and makes it the selection: the table
  selects its row when the search shows it, and otherwise clears its selection
  while the preview shows the similar sample. A sample not analysed yet says
  "Not analysed yet"; a failed query says "No similar samples".

### Organising (3c2b2)

The canvas's "3c2b2: organise and Problems" artboard is the approved design for
what follows.

- **One way to write.** The editor writes through a `LibraryWriter`: rate,
  favourite, add and remove a user tag, create, rename and delete a collection,
  add to and remove from one, save and delete a search, and retry. The
  standalone's writer writes the library directly (short transactions, no writer
  lock); a plugin's runs the `asma` CLI beside it on a background thread, one
  command at a time in the order issued, so the UI never waits on a process and
  the last click wins.
- **Shown at once, confirmed by the library.** A change shows in the UI as it is
  made; the library's change notice then refetches and the stored value replaces
  it. A write that fails (no CLI, the library busy past SQLite's timeout, a name
  taken) rolls back at that refetch, and the footer says why: "Could not save
  the rating: the library is busy".
- **The table:** clicking a row's star favourites it; clicking its rating stars
  sets the rating, and clicking the rating it has clears it. With the table
  focused, `F` toggles the selection's favourite and `0` to `5` sets its rating;
  typing in the search box is never caught.
- **Right-clicking a row** acts on that row (one sample; several at once are not
  in this plan):
  - "Add to collection ▸" lists the collections, ticking those the sample is in;
    picking a ticked one takes it out; "New collection…" makes one and adds the
    sample.
  - "Tags…" opens a popover: the user's tags as chips with a × that removes
    them, the analyser's and the file's own greyed (they cannot be removed), and
    a field that suggests the library's tags with their counts; Return adds one.
    Tags are trimmed and compared ignoring case, so " Bass" adds nothing to a
    sample tagged "bass".
  - "Show in Finder" (Explorer on Windows, the file manager on Linux).
- **The sidebar:** a "+" beside COLLECTIONS adds a name field in place; Return
  keeps it, Escape drops it. Right-clicking a collection or a saved search gives
  "Rename…" (in place, the same way) and "Delete". Deleting never touches
  samples, and asks first only when the collection holds any. Deleting the entry
  the sidebar has lit falls back to All samples.
- **Save search** (the chip row's right end) opens a popover with a name field
  and saves the search in force: scope, chips, text and sort.
- **Names** of collections and saved searches are trimmed, may not be empty and
  are unique within their kind. A taken name is refused inline, with Save or
  Return doing nothing; renaming to the name it already has is no change, not a
  refusal.
- **Problems** takes the table's place until another sidebar entry is picked.
  Each row gives the file, its folder, what went wrong and a "Retry" button;
  "Retry all" sits in the header. Retrying runs `asma retry` (the app and the
  plugin alike): it clears the files' failure and re-reads and re-analyses just
  those files, under the writer lock, waiting while a scan holds it (the footer
  says "Retrying after the scan"). A file that now works leaves the list; one
  that fails again stays with its new reason; one that has gone says "The file
  is gone". When the list empties, the panel says so and Problems leaves the
  sidebar.

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
- **Database corruption:** checked when the first asma window in a process opens
  the library, with SQLite's `PRAGMA quick_check` on a background thread (the
  same page and record damage as `integrity_check`, without verifying index
  contents: seconds, not a minute, on a large library); a query that fails with
  "database disk image is malformed" later leads to the same path. A damaged
  library is rebuilt without asking, by `asma repair` (a helper, so a plugin can
  start it without writing inside the host): it takes the writer lock, checks
  again and never moves a healthy library; renames `library.db` and its `-wal`
  and `-shm` to `library.db.corrupt` (dated when one exists), deleting nothing;
  creates a new library with the backup's folders (or, with no backup, those the
  damaged file still yields), scans them and restores the backup. The footer
  says so: "The library was damaged and has been rebuilt; your ratings and
  collections were restored from 7 October." A file at the library's path that
  is not a database at all counts as damaged. On Windows a file another asma
  process holds open cannot be moved: the rebuild waits and is tried again until
  it is free.
- **Daily backup:** once a day while asma runs, `asma backup` writes
  `backup.json` beside the library (written aside, then renamed into place),
  keeping the day before's as `backup-previous.json`. It holds the folders; each
  organised sample by content hash and size with its rating, favourite and user
  tags; collections with their members by hash; and saved searches, with folder
  and collection references by path and name. `asma restore FILE` applies one by
  hand. Restoring matches samples by content, so moved files keep their data,
  and a sample present twice gets it twice; entries that match nothing are
  counted in the footer ("12 organised samples were not found"), and stay in the
  backup and the `.corrupt` file. Unknown fields from another asma version are
  ignored. `asma check` runs the check by hand.
- **File operation failure mid-group:** execution stops, the group is left
  partially `done`, and the user is offered undo of the completed part.
- **Trash unavailable:** delete refused, no fallback.
- **Streaming failure:** anything reading ahead in a streamed file throws ends
  streaming for that file only: it plays silence where it could not read, and
  the loader carries on.
- **A project's sample has gone:** the processor drops the selection rather than
  keep playing the previous sample; the saved path stays in the project in case
  the drive comes back.
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
| efsw                              | folder change notices            | MIT                   |
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
- **Writers:** one set of tests runs against both `LibraryWriter`s, the CLI one
  driving the built `asma` over a temporary library, and both must leave the
  library the same. A plugin's processor gets the CLI writer, the standalone's
  the direct one.
- **UI fidelity:** a headless test renders the editor at 1280×800 with fixed
  demo data and compares it with `tests/ui/reference/main.png` (the approved
  design), failing above a set mismatch; the same test covers the Key popover,
  the Tags popover and the Problems panel against their own reference pictures.
  The row menu is a native popup menu, which cannot be drawn headless: the
  behaviour tests cover it. It runs on macOS only, since font rendering differs
  between systems; the behaviour tests run everywhere.
- **Plugin validation:** pluginval at strictness 10 (VST3 everywhere, AU on
  macOS) and clap-validator (CLAP) in CI on all three platforms.
  clap-validator's `param-conversions` test is skipped: it divides by the
  parameter count, and asma has no parameters.
- **Watching and the safety net:** the watcher on real temporary folders
  (waiting up to a few seconds for each report, since notices lag on CI); the
  keeper with a fake clock and a fake scan runner; one end-to-end test where a
  file dropped into a watched folder reaches the editor's table; backup and
  restore round trips (moved and renamed files, saved searches' references,
  duplicates, unmatched entries, a crash mid-write); `asma repair` refusing a
  healthy library and rebuilding one corrupted through a second SQLite
  connection; two processes starting on a damaged library, only one rebuilding;
  a library locked by a scan never taken for a damaged one.
- **Performance:** scan, query and Similar timings over a synthetic 50k-file
  library, reported in CI (informational, not gating). Similar runs on the
  message thread while it stays under 50 ms there; past that it moves to a
  worker.
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

## 15. Playlists and processing (between plan 4 and plan 5)

asma follows YouTube playlists the user chooses, fetches new videos' audio as
FLAC through the `yt-dlp` already on the system, and processes each new track
into stems and drum one-shots, all landing in folders the watcher indexes. It
comes in four parts, each with its own plan and merge, in this order, after plan
4 (file manager) and before plan 5 (packaging):

1. **Playlist follower** (designed below).
2. **Processing pipeline:** resumable stages per new track, each skipped when
   its outputs exist, in a helper process; results beside the track as
   `<track>/stems/…` and `<track>/oneshots/…`.
3. **One-shot slicer:** drum hits sliced from a drum stem in C++: onset
   detection, starts anchored on the transient, tail cuts, and quality gates
   (crest factor, spectral flatness, isolation from the rest of the mix, decay,
   a floor below the loudest hit). Useful on any drum stem before part 4.
4. **Built-in stem separation (last):** a mix into vocals, drums, bass, guitar,
   piano and other; drums into kick, snare, toms, hi-hat, ride and crash; vocals
   into lead and backing; through ONNX Runtime in C++ (CPU, Core ML, DirectML),
   models downloaded on first use and checked. Before its design: the models'
   licences must allow a GPLv3 app to download and run them, and exporting
   Roformer models to ONNX may need a spike.

### Playlist follower (part 1)

- **Scope:** public and unlisted playlists only. asma never handles a login and
  never passes browser cookies; a private playlist is refused with "This
  playlist is private; make it unlisted to follow it."
- **Data:** a `playlists` table (a schema migration): the URL (unique), its
  title, its folder (a library root), when it was last checked and the last
  error. The videos already fetched are yt-dlp's own download archive, at
  `playlists/<id>.archive` in the data directory.
- **Commands:** `asma playlist add URL [--folder DIR]` asks yt-dlp for the
  playlist's title without downloading (which also tells a playlist from
  anything else), makes the folder (default `~/Music/asma/YouTube/<title>`, the
  title made safe as a file name), adds it to the library and records the
  playlist; adding the same URL again is refused. `asma playlist list`;
  `asma playlist remove` stops following and leaves the folder and its files.
  `asma playlist check [--id N]` runs yt-dlp on each playlist
  (`-x --audio-format flac --download-archive <archive>`, with a progress
  template) and prints JSON lines: found N new, downloading k of N with the
  title and its progress, done, failed and why.
- **Downloading:** into a hidden staging folder inside the playlist's folder,
  which the watcher ignores, each finished FLAC then moved into place as
  `<title> [<video id>].flac`, so the watcher only ever sees whole files. A
  title is made safe as a file name and cut to fit. A video removed from the
  playlist keeps its file; the same video in two playlists is fetched into each.
  A check cut short leaves part files in the staging folder, which the next
  check clears.
- **Honest about quality:** YouTube's audio is lossy (Opus or AAC); the FLAC
  keeps it intact without a second lossy step, and asma never presents it as
  lossless audio.
- **When:** while any asma window is open, the keeper runs `asma playlist check`
  through the helper at startup, every 6 hours, and on "Check now", one playlist
  at a time. A plugin does this through `asma-cli`, as every write; nothing
  downloads inside a host.
- **Finding the tools:** `yt-dlp` and `ffmpeg` on the PATH and in the usual
  places (`/opt/homebrew/bin`, `/usr/local/bin`, `~/.local/bin`, WinGet's links
  folder on Windows; apps started from the macOS Finder do not get the shell's
  PATH), overridden by `ASMA_YTDLP` and `ASMA_FFMPEG`. asma never downloads or
  updates yt-dlp.
- **UI:** "Follow a playlist…" beside "Add folder…" in the app, and in the
  sidebar's "+" menu in a plugin, opens a popover with the URL and the folder
  (its default shown, "Choose…" to change it); Follow checks the URL through the
  helper, shows the title and starts the first check; a bad or private URL is
  refused in red in the popover. A playlist's folder stays under FOLDERS,
  marked, with a count of new downloads since it was last looked at;
  right-clicking it gives "Check now" and "Stop following…" (which says the
  files stay). The footer says "Checking 2 playlists…", "Downloading 2 of 5:
  <title>" and "3 new from <playlist>". With `yt-dlp` or `ffmpeg` missing, the
  popover says which and how to get it (`brew install yt-dlp`, `winget install
  yt-dlp`, the Linux package manager), with "Look again".
- **Errors:** a failed check (network, yt-dlp out of date, the playlist gone)
  keeps yt-dlp's message on the playlist, shown in red in the sidebar with
  "updating yt-dlp often fixes this", until a check succeeds; it is tried again
  at the next check, never in a loop. A video that fails on its own (blocked,
  removed, age-restricted) is retried at later checks; after three failures it
  is listed under Problems with yt-dlp's reason. A playlist whose folder was
  deleted is not fetched into until the folder is back or chosen again.
- **Testing:** a fake yt-dlp (the test helper binary, through `ASMA_YTDLP`)
  plays a playlist of three, a new video, a private playlist, a single video
  instead of a playlist, a failing video, a download cut short and "out of
  date"; the tests never reach YouTube. They check whole FLACs with safe names,
  nothing fetched twice, failures recorded and retried, a third failure in
  Problems, remove keeping the files, the JSON progress reaching the footer,
  finding the tools, the migration, and the popover and sidebar. One hand test
  follows a real public playlist, since only YouTube shows whether yt-dlp still
  behaves as expected.

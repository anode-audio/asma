# asma

Anode Labs Sample Manager. Free, open-source sample manager for macOS, Windows
and Linux. Work in progress.

## Build

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure

## Command line

    asma root add ~/Samples
    asma scan                    # index, then analyse new and changed files
    asma query kick
    asma query --type loop --bpm 120-130 --key Am
    asma query dusty --tag drums --json
    asma similar ~/Samples/Drums/Kick_01.wav

Ratings, favourites, tags, collections and saved searches:

    asma rate 5 ~/Samples/Drums/Kick_01.wav     # 0 clears; --id N works too
    asma fav on ~/Samples/Drums/Kick_01.wav
    asma tag add punchy ~/Samples/Drums/Kick_01.wav
    asma collection create "Live set"
    asma collection add "Live set" ~/Samples/Drums/Kick_01.wav
    asma query --collection "Live set" --min-rating 4 --favourites
    asma search save "Fast loops" --type loop --bpm 140-180
    asma query --saved "Fast loops" --sort rating --desc

The library lives in the platform data directory (on macOS
`~/Library/Application Support/Anode Labs/asma`); `--db PATH` or the
`ASMA_DATA_DIR` environment variable override it.

## Analysis

`asma scan` analyses each new or changed file once: loudness (EBU R128), tempo,
key, loop or one-shot, and a timbre fingerprint for `asma similar`. BPM, key and
loop flags found in the file (ACID chunks) or its name always win over analysis.

Measured on a real 70,000-file library against the BPM and key that file names
already state: across 287 labelled loops, tempo exact for 52% and off by exactly
double or half for another 23% (half-time grooves are labelled both ways); key
right for 69% of the files it reports a key for. To measure your own library
(nothing is uploaded or copied):

    ASMA_EVAL_DIR=~/Samples ./build/tests/asma_tests "[.real]"

## Audition and drag-out

`asma_audio` is the audition engine the app and plugin play through, built
without JUCE so it runs and tests headless: trim, reverse and ping-pong, loops
stretched to the host tempo, transpose to the project key, loudness matching and
eight MIDI voices. `asma render` prints the file a drag should carry: the
original when nothing changes the audio, otherwise a render with the edits baked
in.

    asma render ~/Samples/Loops/Funk_96.wav --tempo 120   # stretched, same pitch
    asma render ~/Samples/Keys/Rhodes_Am.wav --key C#m --reverse
    asma render ~/Samples/Drums/Kick_01.wav               # no edits: the file itself

Sync only happens when asma is sure of the sample's tempo or key; otherwise it
says so and leaves the sample alone. Renders live in the platform cache
directory (on macOS `~/Library/Caches/Anode Labs/asma/renders`), capped at 2 GB;
`--cache DIR` or the `ASMA_CACHE_DIR` environment variable override it.

## License

GPLv3. See `LICENSE`.

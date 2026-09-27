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

## License

GPLv3. See `LICENSE`.

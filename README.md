# asma

Anode Labs Sample Manager. Free, open-source sample manager for macOS, Windows
and Linux. Work in progress.

## Build

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure

## Command line

    asma root add ~/Samples
    asma scan
    asma query kick
    asma query --type loop --bpm 120-130 --key Am
    asma query dusty --tag drums --json

The library lives in the platform data directory (on macOS
`~/Library/Application Support/Anode Labs/asma`); `--db PATH` or the
`ASMA_DATA_DIR` environment variable override it.

## License

GPLv3. See `LICENSE`.

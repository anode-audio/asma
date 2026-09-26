# Test fixtures

Half-second 440 Hz sine tones generated with ffmpeg. They contain no third-party
audio.

    SRC="sine=frequency=440:sample_rate=44100:duration=0.5"
    ffmpeg -f lavfi -i "$SRC" -ac 1 -c:a flac -sample_fmt s16 tone.flac
    ffmpeg -f lavfi -i "$SRC" -ac 1 -c:a libmp3lame -b:a 64k tone.mp3
    ffmpeg -f lavfi -i "$SRC" -ac 2 -c:a vorbis -strict experimental tone.ogg

# UI reference

`main.png` is what `tests/plugin/test_ui_fidelity.cpp` compares the editor with.
It is the approved design's main artboard (a private design canvas, "asma 3c2a
UI") rebuilt as `main.html`, with these changes and nothing else:

- The rows, the search, the count, the file line and the tempo are the test's
  demo library and state, not the design's sample data.
- The areas plan 3c2b fills are blank: the sidebar, the chip row, the Similar
  list, and the favourite, rating and tags columns.
- The sample is stopped (a play button, no playhead), and the Key and Start
  chips say what they say when off ("off", "now"), so the picture is
  deterministic.
- The footer's right side is empty: the test has no scan to report and no
  renders.

The test leaves out the waveform's own shape (the demo audio is not the
design's) and the window's resize corner.

## Making it again

After a deliberate change to the design, edit `main.html` and render it with
Chrome at exactly 1280×800, scale 1:

```sh
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
  --window-size=1280,800 --screenshot="$PWD/main.png" "file://$PWD/main.html"
```

`main.html` loads the fonts from `plugin/fonts`, the same files the app embeds.
The test writes `current.png` and `diff.png` (mismatched pixels in magenta) to
`$ASMA_UI_OUT`, or `asma-ui` in the system's temporary directory.

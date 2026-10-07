# UI reference

`main.png` is what `tests/plugin/test_ui_fidelity.cpp` compares the editor with.
It is the approved design's main artboard (a private design canvas, "asma 3c2a
UI") rebuilt as `main.html`, with these changes and nothing else:

- The rows, the search, the count, the file line and the tempo are the test's
  demo library and state, not the design's sample data.
- The sidebar, the chip row, the ratings and favourites and the Similar list
  show the demo library: two folders, a collection, two saved searches, the
  design's ratings and favourites, its two active filters, and the neighbours
  the demo's analysis finds.
- The sample is stopped (a play button, no playhead), and the Key and Start
  chips say what they say when off ("off", "now"), so the picture is
  deterministic.
- The footer's right side is empty: the test has no scan to report and no
  renders.

`key-popover.png` is the design's Key popover from the canvas's popovers
artboard, rebuilt as `key-popover.html` at the size the app gives it (360×180),
with C and Am picked.

`tags-popover.png` is the design's Tags popover from the canvas's organise
artboard, rebuilt as `tags-popover.html` at the size the app gives it (360×216),
"gr" typed. The field is drawn unfocused (the test cannot give it focus) and the
hint is the app's one line.

`problems.png` is the design's Problems panel from the same artboard, rebuilt as
`problems.html` at 840×212 with three files, the second retrying.

All the pictures are blurred before they are compared, so the test checks where
text and shapes are rather than how each renderer draws their edges. Each area
has its own limit, recorded in the test with the figures it measured. The test
leaves out the waveform's own shape (the demo audio is not the design's) and the
window's resize corner.

## Making it again

After a deliberate change to the design, edit `main.html` and render it with
Chrome at exactly 1280×800, scale 1:

```sh
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
  --window-size=1280,800 --screenshot="$PWD/main.png" "file://$PWD/main.html"
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
  --window-size=360,180 --screenshot="$PWD/key-popover.png" "file://$PWD/key-popover.html"
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
  --window-size=360,216 --screenshot="$PWD/tags-popover.png" "file://$PWD/tags-popover.html"
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new \
  --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
  --window-size=840,212 --screenshot="$PWD/problems.png" "file://$PWD/problems.html"
```

`main.html` loads the fonts from `plugin/fonts`, the same files the app embeds.
The test writes `current.png` and `diff.png` (mismatched pixels in magenta) to
`$ASMA_UI_OUT`, or `asma-ui` in the system's temporary directory.

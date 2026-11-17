# Detail Clipper

A hard clipper that keeps the detail, in the Cicada look (`projects/shared/cicada_ui`).

The part of the signal that the clipper removes is high-passed and partly mixed
back in, so mostly the low-frequency body of each peak is flattened while
transients and air pass through. Clipping runs at up to 16x oversampling with
first-order antiderivative anti-aliasing (ADAA).

## Controls

| Section | Controls |
| --- | --- |
| Clip | Drive, Ceiling, Oversampling (1x to 16x), ADAA, clip meter |
| Shape | Knee (hard to rounded), Asymmetry (even harmonics), Stereo (Left/Right or Mid/Side), transfer curve |
| Detail | Detail amount, Freq (where restoration starts), Headroom (how far restored detail may exceed the ceiling), True Ceiling |
| Output | Output gain, Mix (latency compensated), Auto Gain, Delta (hear only what is removed), scope |

Double-click a knob to reset it. Each panel has a Reset button, and the header has Reset all.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target DetailClipper_VST3
```

The plug-in lands in `build/DetailClipper_artefacts/Release/VST3/`. Copy
`Detail Clipper.vst3` to your VST3 folder:

- Windows: `C:\Program Files\Common Files\VST3`
- macOS: `~/Library/Audio/Plug-Ins/VST3` (the AU goes in `~/Library/Audio/Plug-Ins/Components`)
- Linux: `~/.vst3`

The `Detail Clipper` GitHub Actions workflow builds Windows and macOS versions
on every push that touches this folder or `cicada_ui`, and attaches them to
the run as downloadable artifacts. The macOS build is only ad-hoc signed: on
another Mac, right-click it and choose Open, or clear the quarantine flag.

The DSP is shared with the single-file example
`examples/Plugins/DetailClipperPluginDemo.h`.

JUCE is dual licensed under AGPLv3 and a commercial licence. Distributing this
plug-in outside the AGPLv3 terms needs a commercial JUCE licence.

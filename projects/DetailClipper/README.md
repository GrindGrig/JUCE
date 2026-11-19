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

The `Detail Clipper` GitHub Actions workflow builds a Windows x64 package
(VST3, standalone app and Install.txt) on every push that touches this folder
or `cicada_ui`, and attaches it to the run.

To publish a download for everyone, bump the version in `CMakeLists.txt`, then
either push a matching tag (for example `detail-clipper-v1.0.0`) or start the
workflow from the Actions tab with **release** ticked. The workflow creates
the GitHub Release `detail-clipper-v<version>` with the zip attached; the web
demo's download button links to it.

The macOS build (VST3, AU, Standalone, universal for Apple silicon and Intel)
runs for release tags, or when the workflow is started by hand with **macos**
ticked; when it runs, the release gets `DetailClipper-<version>-macOS.zip` too.
It also runs Apple's `auval` check on the AU. The build is only ad-hoc signed,
not notarized, so on another Mac the plug-ins need their quarantine flag
cleared (the zip's Install.txt has the Terminal command). Notarizing needs an
Apple Developer account.

The DSP is shared with the single-file example
`examples/Plugins/DetailClipperPluginDemo.h`.

JUCE is dual licensed under AGPLv3 and a commercial licence. Distributing this
plug-in outside the AGPLv3 terms needs a commercial JUCE licence.

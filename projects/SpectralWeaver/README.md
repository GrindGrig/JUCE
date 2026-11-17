# Spectral Weaver

![Spectral Weaver](screenshot.png)

A JUCE instrument plug-in (VST3 / AU / Standalone). It takes a sample apart
into its **spectral image**, which is the magnitude and true frequency of every
STFT bin in every frame. It then resynthesises that image as a playable,
polyphonic sound source. Drop a **second sample** on slot B and it reshapes the
first one with its own spectrum.

## How it works

1. **Analysis** (background thread, on drop): the file is decoded, mixed to mono,
   resampled to 48 kHz and normalised. A 2048-point Hann STFT with 75 % overlap
   measures each bin's magnitude and its instantaneous frequency (from the
   phase difference between frames). The result is shown as a log-frequency
   spectrogram.
2. **Resynthesis** (per voice, 8 voices): a playhead scans through the image at
   any speed or direction, or freezes on one moment. Each hop, the frame passes
   through the cross-modulation and sculpting stages. Its bins are then moved
   to the played pitch and rebuilt with a phase vocoder that uses identity
   phase locking, so partials stay clean.

## Controls

| Section | Controls |
|---|---|
| **Playback / Scan** | Start, Length (scan region, shown on the spectrogram), Speed (−4x … 4x, negative = reverse), Freeze (scrub with Start), Loop, Root note, Tune, Pitch-bend range, Key Track |
| **B → A Cross Modulation** | **Vocode**: A's fine structure wearing B's spectral envelope. **Multiply**: A filtered by B, bin by bin. **Morph**: geometric interpolation of the two images. **Mask**: B's energy gates A's bins. **Imprint**: B's harmonics/pitch coloured by A's envelope. Plus Amount (the mod wheel adds to it), Envelope Width, Sync to A (B follows A's playhead) or a free-running B Speed |
| **Spectral Sculpting** | Blur (temporal smoothing), Smear (frequency smoothing), Tonal (+ keeps only spectral peaks, − keeps only the residual noise), Tilt (dB/oct), Gate (drop bins below a threshold), Warp (inharmonic frequency stretch), Shift (linear Hz shift), Diffuse (phase randomisation) |
| **LFO** | Rate, Depth, Shape (sine/tri/saw/square/random), Target (Position, Pitch, Warp, Shift, Tilt, Cross Amount, Smear, Blur) |
| **Amp** | ADSR, Gain |

To load a sample, drag an audio file (WAV/AIFF/FLAC/OGG/MP3, up to 60 s) onto
a spectrogram or click it to browse. Right-click a spectrogram to clear it.
File paths are saved with the session.

## Building

From this directory (it uses the JUCE checkout two levels up):

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

The VST3, AU (macOS) and Standalone app are written to
`build/SpectralWeaver_artefacts/Release/`. On Linux, first install the packages
listed in `docs/Linux Dependencies.md`.

### DSP tests

    cmake -B build -DSPECTRAL_WEAVER_BUILD_TESTS=ON
    cmake --build build --target SpectralWeaverTests
    ./build/SpectralWeaverTests_artefacts/Release/SpectralWeaverTests

These check analysis accuracy, resynthesis pitch and level across sample
rates, key tracking, extreme settings and every cross-modulation mode.

## Licensing

This plug-in is built on the JUCE framework, which is dual-licensed under the
AGPLv3 and the commercial JUCE licence (see `../../LICENSE.md`). If you
distribute this plug-in without releasing its source under the AGPLv3, for
example as a closed-source commercial product, you need a commercial JUCE
licence.

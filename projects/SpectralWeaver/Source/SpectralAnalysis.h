#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_dsp/juce_dsp.h>

namespace sw
{

/** Fixed STFT settings shared by analysis and resynthesis. */
struct Stft
{
    static constexpr int    order      = 11;
    static constexpr int    size       = 1 << order;     // 2048
    static constexpr int    numBins    = size / 2 + 1;   // 1025
    static constexpr int    hop        = size / 4;       // 512 (75% overlap)
    static constexpr double sampleRate = 48000.0;        // every sample is analysed at this rate
    static constexpr double maxSeconds = 60.0;

    static constexpr float binHz() noexcept { return (float) (sampleRate / size); }
};

/** The "spectral image" of a sample: a magnitude + instantaneous-frequency
    value for every STFT bin of every frame.

    Instances are immutable once built and shared between the message thread
    and the audio thread through a reference-counted pointer.
*/
struct SpectralImage final : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SpectralImage>;

    juce::File   file;
    juce::String name;
    double       lengthSeconds = 0.0;
    int          numFrames     = 0;

    std::vector<float> magnitudes;   // numFrames * numBins, linear |X|
    std::vector<float> frequencies;  // numFrames * numBins, instantaneous frequency in Hz
    std::vector<float> frameMax;     // loudest bin of each frame
    float              peak = 0.0f;  // loudest bin of the whole image
    float              samplePeak = 1.0f; // absolute sample peak of the decoded audio (1.0 = 0 dBFS)

    juce::Image picture;             // log-frequency spectrogram for display

    const float* mag  (int frame) const noexcept { return magnitudes.data()  + (size_t) frame * Stft::numBins; }
    const float* freq (int frame) const noexcept { return frequencies.data() + (size_t) frame * Stft::numBins; }
};

/** Analyses already-decoded mono audio at Stft::sampleRate. */
SpectralImage::Ptr analyseSamples (const float* samples, int numSamples);

/** Decodes, resamples and analyses an audio file. Returns nullptr and fills
    errorMessage on failure. Safe to call from a background thread.
*/
SpectralImage::Ptr analyseFile (const juce::File& file,
                                juce::AudioFormatManager& formats,
                                juce::String& errorMessage);

/** Renders the display picture for an image (called by the analysers). */
juce::Image renderSpectrogram (const SpectralImage& image, int width, int height);

} // namespace sw

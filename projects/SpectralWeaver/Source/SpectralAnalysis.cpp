#include "SpectralAnalysis.h"

namespace sw
{

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    float wrapPhase (float p) noexcept
    {
        return p - twoPi * std::floor ((p + juce::MathConstants<float>::pi) / twoPi);
    }

    // Perceptual colour ramp (dark violet -> magenta -> orange -> pale yellow)
    juce::Colour heatColour (float v) noexcept
    {
        struct Stop { float pos; juce::uint32 argb; };
        static constexpr Stop stops[] = { { 0.00f, 0xff07050f }, { 0.25f, 0xff2b0f54 },
                                          { 0.50f, 0xff8c2981 }, { 0.75f, 0xfff0605d },
                                          { 1.00f, 0xfffcf6b6 } };
        v = juce::jlimit (0.0f, 1.0f, v);

        for (size_t i = 1; i < std::size (stops); ++i)
        {
            if (v <= stops[i].pos)
            {
                const auto t = (v - stops[i - 1].pos) / (stops[i].pos - stops[i - 1].pos);
                return juce::Colour (stops[i - 1].argb).interpolatedWith (juce::Colour (stops[i].argb), t);
            }
        }

        return juce::Colour (stops[std::size (stops) - 1].argb);
    }
}

SpectralImage::Ptr analyseSamples (const float* samples, int numSamples)
{
    SpectralImage::Ptr image = new SpectralImage();

    constexpr int N   = Stft::size;
    constexpr int hop = Stft::hop;
    constexpr int nb  = Stft::numBins;

    const int numFrames = juce::jmax (1, (numSamples + hop - 1) / hop);
    image->numFrames     = numFrames;
    image->lengthSeconds = numSamples / Stft::sampleRate;
    image->magnitudes .assign ((size_t) numFrames * nb, 0.0f);
    image->frequencies.assign ((size_t) numFrames * nb, 0.0f);
    image->frameMax   .assign ((size_t) numFrames, 0.0f);

    juce::dsp::FFT fft (Stft::order);
    std::vector<float> window ((size_t) N), buffer ((size_t) N * 2), lastPhase ((size_t) nb, 0.0f);

    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) N); // periodic Hann

    const float expectedAdvance = twoPi * (float) hop / (float) N; // phase advance per bin index
    const float binHz = Stft::binHz();

    for (int f = 0; f < numFrames; ++f)
    {
        // Frames are centred on f * hop
        const int start = f * hop - N / 2;
        std::fill (buffer.begin(), buffer.end(), 0.0f);

        for (int i = 0; i < N; ++i)
        {
            const int idx = start + i;

            if (idx >= 0 && idx < numSamples)
                buffer[(size_t) i] = samples[idx] * window[(size_t) i];
        }

        fft.performRealOnlyForwardTransform (buffer.data(), true);

        auto* mags  = image->magnitudes .data() + (size_t) f * nb;
        auto* freqs = image->frequencies.data() + (size_t) f * nb;
        float fmax = 0.0f;

        for (int k = 0; k < nb; ++k)
        {
            const float re = buffer[(size_t) (2 * k)];
            const float im = buffer[(size_t) (2 * k + 1)];
            const float m  = std::sqrt (re * re + im * im);
            const float ph = std::atan2 (im, re);

            float trueBin = (float) k;

            if (f > 0)
            {
                const float delta = wrapPhase (ph - lastPhase[(size_t) k] - expectedAdvance * (float) k);
                trueBin += delta / expectedAdvance;
            }

            lastPhase[(size_t) k] = ph;
            mags[k]  = m;
            freqs[k] = juce::jmax (0.0f, trueBin * binHz);
            fmax = juce::jmax (fmax, m);
        }

        image->frameMax[(size_t) f] = fmax;
        image->peak = juce::jmax (image->peak, fmax);
    }

    image->picture = renderSpectrogram (*image, juce::jlimit (64, 1200, numFrames), 256);
    return image;
}

SpectralImage::Ptr analyseFile (const juce::File& file, juce::AudioFormatManager& formats, juce::String& errorMessage)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        errorMessage = "Unsupported or unreadable file: " + file.getFileName();
        return nullptr;
    }

    if (reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
    {
        errorMessage = "File contains no audio: " + file.getFileName();
        return nullptr;
    }

    const auto maxSourceSamples = (juce::int64) (Stft::maxSeconds * reader->sampleRate);
    const int  sourceLength     = (int) juce::jmin (reader->lengthInSamples, maxSourceSamples);
    const int  numChannels      = (int) juce::jlimit (1u, 8u, reader->numChannels);

    juce::AudioBuffer<float> source (numChannels, sourceLength);
    reader->read (&source, 0, sourceLength, 0, true, numChannels > 1);

    // Mix to mono
    std::vector<float> mono ((size_t) sourceLength, 0.0f);

    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::add (mono.data(), source.getReadPointer (ch), sourceLength);

    juce::FloatVectorOperations::multiply (mono.data(), 1.0f / (float) numChannels, sourceLength);

    // Resample to the analysis rate so every image shares the same bin grid
    std::vector<float> resampled;
    const double ratio = reader->sampleRate / Stft::sampleRate;

    if (std::abs (ratio - 1.0) < 1.0e-6)
    {
        resampled = std::move (mono);
    }
    else
    {
        const int outLength = (int) std::floor ((double) sourceLength / ratio);
        resampled.assign ((size_t) outLength + 8, 0.0f);
        mono.resize (mono.size() + 16, 0.0f); // padding for the interpolator's look-ahead

        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, mono.data(), resampled.data(), outLength);
        resampled.resize ((size_t) outLength);
    }

    if (resampled.size() < (size_t) Stft::hop)
    {
        errorMessage = "Sample is too short: " + file.getFileName();
        return nullptr;
    }

    // Normalise so quiet samples resynthesise at a usable level
    const auto range = juce::FloatVectorOperations::findMinAndMax (resampled.data(), (int) resampled.size());
    const float absPeak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));

    if (absPeak < 1.0e-6f)
    {
        errorMessage = "Sample is silent: " + file.getFileName();
        return nullptr;
    }

    juce::FloatVectorOperations::multiply (resampled.data(), 0.89f / absPeak, (int) resampled.size());

    auto image = analyseSamples (resampled.data(), (int) resampled.size());
    image->file = file;
    image->name = file.getFileName();
    return image;
}

juce::Image renderSpectrogram (const SpectralImage& image, int width, int height)
{
    juce::Image picture (juce::Image::RGB, width, height, true, juce::SoftwareImageType());

    if (image.numFrames <= 0 || image.peak <= 0.0f)
        return picture;

    juce::Image::BitmapData pixels (picture, juce::Image::BitmapData::writeOnly);

    constexpr float minHz = 30.0f, maxHz = 20000.0f, rangeDb = 78.0f;
    const float binHz = Stft::binHz();
    const float peakDb = juce::Decibels::gainToDecibels (image.peak);

    // Pre-compute which bins each row covers (log frequency axis, low at bottom)
    std::vector<std::pair<int, int>> rowBins ((size_t) height);

    for (int y = 0; y < height; ++y)
    {
        const auto hzAt = [&] (float row) { return minHz * std::pow (maxHz / minHz, 1.0f - row / (float) height); };
        const int hi = juce::jlimit (1, Stft::numBins - 1, (int) std::ceil  (hzAt ((float) y)       / binHz));
        const int lo = juce::jlimit (1, Stft::numBins - 1, (int) std::floor (hzAt ((float) y + 1.0f) / binHz));
        rowBins[(size_t) y] = { juce::jmin (lo, hi), juce::jmax (lo, hi) };
    }

    for (int x = 0; x < width; ++x)
    {
        const int f0 = (int) ((juce::int64) x       * image.numFrames / width);
        const int f1 = juce::jmax (f0 + 1, (int) ((juce::int64) (x + 1) * image.numFrames / width));

        for (int y = 0; y < height; ++y)
        {
            float m = 0.0f;

            for (int f = f0; f < f1 && f < image.numFrames; ++f)
            {
                const auto* mags = image.mag (f);

                for (int k = rowBins[(size_t) y].first; k <= rowBins[(size_t) y].second; ++k)
                    m = juce::jmax (m, mags[k]);
            }

            const float db = juce::Decibels::gainToDecibels (m, -200.0f) - peakDb;
            pixels.setPixelColour (x, y, heatColour (std::pow (juce::jlimit (0.0f, 1.0f, 1.0f + db / rangeDb), 1.7f)));
        }
    }

    return picture;
}

} // namespace sw

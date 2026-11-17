#include "SpectralVoice.h"

namespace sw
{

namespace
{
    constexpr int   N     = Stft::size;
    constexpr int   hop   = Stft::hop;
    constexpr int   nb    = Stft::numBins;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // Hann analysis * Hann synthesis windows at 75% overlap sum to 1.5
    constexpr float overlapGain = 1.5f;

    float wrapPhase (float p) noexcept
    {
        return p - twoPi * std::floor (p / twoPi);
    }

    float sumOfSquares (const float* data, int n) noexcept
    {
        double s = 0.0;

        for (int i = 0; i < n; ++i)
            s += (double) data[i] * data[i];

        return (float) s;
    }
}

SpectralVoice::SpectralVoice()
{
    prepare (44100.0);
}

void SpectralVoice::prepare (double sampleRate)
{
    hostRate = sampleRate;
    adsr.setSampleRate (sampleRate);

    window.resize ((size_t) N);

    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) N);

    fftBuffer.assign ((size_t) N * 2, 0.0f);
    ola.assign ((size_t) N, 0.0f);
    ready.assign ((size_t) hop, 0.0f);

    for (auto* v : { &magA, &freqA, &magB, &freqB, &smoothed, &work, &envA, &envB, &outMag, &outFreq, &outBest, &phase })
        v->assign ((size_t) nb, 0.0f);

    prefix.assign ((size_t) nb + 1, 0.0);
    readyPos = hop;
}

bool SpectralVoice::canPlaySound (juce::SynthesiserSound* s)
{
    return dynamic_cast<SpectralSound*> (s) != nullptr;
}

void SpectralVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition)
{
    midiNote     = midiNoteNumber;
    velocityGain = 0.25f + 0.75f * velocity;
    pitchWheelMoved (currentPitchWheelPosition);

    std::fill (ola.begin(), ola.end(), 0.0f);
    std::fill (ready.begin(), ready.end(), 0.0f);
    readyPos   = hop;
    firstHop   = true;
    reachedEnd = false;

    std::fill (phase.begin(), phase.end(), 0.0f);

    posA = posB = 0.0f;

    if (params != nullptr && params->imageA != nullptr)
    {
        posA = params->start * (float) (params->imageA->numFrames - 1);

        if (params->imageB != nullptr && params->syncB)
            posB = params->start * (float) (params->imageB->numFrames - 1);
    }

    adsr.reset();
    adsr.noteOn();
}

void SpectralVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
        adsr.noteOff();
    else
        finish();
}

void SpectralVoice::pitchWheelMoved (int newPitchWheelValue)
{
    const float range = params != nullptr ? params->pitchBendRange : 2.0f;
    bendSemitones = (float) (newPitchWheelValue - 8192) / 8192.0f * range;
}

void SpectralVoice::finish()
{
    adsr.reset();
    clearCurrentNote();
    displayPosA = -1.0f;
    displayPosB = -1.0f;
}

void SpectralVoice::renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    if (! isVoiceActive())
        return;

    if (params == nullptr || params->imageA == nullptr)
    {
        finish();
        return;
    }

    const float gain = params->gain * velocityGain;
    const int numChannels = output.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (readyPos >= hop)
        {
            synthesiseHop();
            readyPos = 0;
        }

        const float sample = ready[(size_t) readyPos++] * adsr.getNextSample() * gain;

        for (int ch = 0; ch < numChannels; ++ch)
            output.addSample (ch, startSample + i, sample);

        if (! adsr.isActive())
        {
            finish();
            break;
        }
    }
}

//==============================================================================
void SpectralVoice::synthesiseHop()
{
    const auto& p = *params;
    const auto& a = *p.imageA;
    const auto* b = p.imageB;

    readFrame (a, posA, magA.data(), freqA.data());

    // Temporal blur: one-pole smoothing of the magnitude image over time
    if (firstHop || p.blur <= 0.0f)
    {
        std::copy (magA.begin(), magA.end(), smoothed.begin());
    }
    else
    {
        const float c = 0.997f * std::pow (p.blur, 0.35f);

        for (int k = 0; k < nb; ++k)
            smoothed[(size_t) k] = smoothed[(size_t) k] * c + magA[(size_t) k] * (1.0f - c);
    }

    std::copy (smoothed.begin(), smoothed.end(), work.begin());

    if (b != nullptr && p.crossAmount > 0.0f)
    {
        readFrame (*b, posB, magB.data(), freqB.data());
        applyCrossModulation (*b);
    }

    applySculpting();
    remapAndSynthesise();
    advancePlayheads (a, b);
    firstHop = false;

    displayPosA = a.numFrames > 1 ? posA / (float) (a.numFrames - 1) : 0.0f;
    displayPosB = (b != nullptr && b->numFrames > 1) ? posB / (float) (b->numFrames - 1) : -1.0f;

    if (reachedEnd && adsr.isActive())
        adsr.noteOff();
}

void SpectralVoice::readFrame (const SpectralImage& image, float position, float* magsOut, float* freqsOut) const
{
    const int   last = image.numFrames - 1;
    const float pos  = juce::jlimit (0.0f, (float) last, position);
    const int   i0   = (int) pos;
    const int   i1   = juce::jmin (i0 + 1, last);
    const float t    = pos - (float) i0;

    const float* m0 = image.mag (i0);
    const float* m1 = image.mag (i1);

    for (int k = 0; k < nb; ++k)
        magsOut[k] = m0[k] + t * (m1[k] - m0[k]);

    const float* f = image.freq (t < 0.5f ? i0 : i1);
    std::copy (f, f + nb, freqsOut);
}

void SpectralVoice::advancePlayheads (const SpectralImage& a, const SpectralImage* b)
{
    const auto& p = *params;
    const float framesPerHop = (float) (Stft::sampleRate / hostRate); // analysis frames per output hop at speed 1
    const float lastA = (float) (a.numFrames - 1);

    const float regionStart = juce::jlimit (0.0f, lastA, p.start * lastA);
    const float regionEnd   = juce::jmin (lastA, regionStart + juce::jmax (1.0f, p.length * (lastA - regionStart)));
    const float regionLen   = regionEnd - regionStart;

    if (p.freeze)
    {
        posA = regionStart;
    }
    else
    {
        posA += p.speed * framesPerHop;

        if (p.loop)
        {
            if (regionLen < 1.0e-3f)
                posA = regionStart;
            else if (posA > regionEnd || posA < regionStart)
                posA = regionStart + std::fmod (std::fmod (posA - regionStart, regionLen) + regionLen, regionLen);
        }
        else if (posA > regionEnd || posA < regionStart)
        {
            posA = juce::jlimit (regionStart, regionEnd, posA);
            reachedEnd = true;
        }
    }

    if (b != nullptr)
    {
        const float lastB = (float) (b->numFrames - 1);

        if (p.syncB)
        {
            posB = lastA > 0.0f ? posA / lastA * lastB : 0.0f;
        }
        else if (lastB > 0.0f)
        {
            posB += p.speedB * framesPerHop;
            posB = std::fmod (std::fmod (posB, lastB) + lastB, lastB);
        }
        else
        {
            posB = 0.0f;
        }
    }
}

void SpectralVoice::boxSmooth (const float* in, float* out, int radius, std::vector<double>& scratch)
{
    scratch[0] = 0.0;

    for (int k = 0; k < nb; ++k)
        scratch[(size_t) k + 1] = scratch[(size_t) k] + in[k];

    for (int k = 0; k < nb; ++k)
    {
        const int lo = juce::jmax (0, k - radius);
        const int hi = juce::jmin (nb, k + radius + 1);
        out[k] = (float) ((scratch[(size_t) hi] - scratch[(size_t) lo]) / (double) (hi - lo));
    }
}

void SpectralVoice::applyCrossModulation (const SpectralImage& b)
{
    const auto& p   = *params;
    const float amt = juce::jlimit (0.0f, 1.0f, p.crossAmount);
    const float ref = juce::jmax (params->imageA->peak, b.peak, 1.0e-9f);
    const float eps = ref * 1.0e-4f;

    switch (p.crossMode)
    {
        case CrossMode::vocode:
        {
            // A keeps its fine structure, but wears B's spectral envelope
            boxSmooth (work.data(), envA.data(), p.envelopeWidth, prefix);
            boxSmooth (magB.data(), envB.data(), p.envelopeWidth, prefix);

            for (int k = 0; k < nb; ++k)
            {
                const float ratio = juce::jlimit (0.0f, 1000.0f, (envB[(size_t) k] + eps) / (envA[(size_t) k] + eps));
                work[(size_t) k] *= std::pow (ratio, amt);
            }
            break;
        }

        case CrossMode::multiply:
        {
            // Spectral product: A filtered by B, level-compensated back towards A's energy
            const float energyBefore = sumOfSquares (work.data(), nb);

            for (int k = 0; k < nb; ++k)
                work[(size_t) k] *= std::pow (magB[(size_t) k] / ref, amt);

            const float energyAfter = sumOfSquares (work.data(), nb);

            if (energyAfter > 1.0e-20f)
            {
                const float makeup = juce::jlimit (1.0f, 16.0f, std::sqrt (energyBefore / energyAfter));
                juce::FloatVectorOperations::multiply (work.data(), makeup, nb);
            }
            break;
        }

        case CrossMode::morph:
        {
            // Geometric interpolation of both images; partial frequencies follow the dominant side
            const float floor = ref * 1.0e-5f;

            for (int k = 0; k < nb; ++k)
            {
                const float ma = juce::jmax (work[(size_t) k], floor);
                const float mb = juce::jmax (magB[(size_t) k], floor);
                work[(size_t) k] = std::exp ((1.0f - amt) * std::log (ma) + amt * std::log (mb)) - floor;

                if (amt * mb > (1.0f - amt) * ma)
                    freqA[(size_t) k] = freqB[(size_t) k];
            }
            break;
        }

        case CrossMode::mask:
        {
            // B's energy (in dB below its own peak) opens and closes A's bins
            for (int k = 0; k < nb; ++k)
            {
                const float db   = juce::Decibels::gainToDecibels (magB[(size_t) k] / ref, -200.0f);
                const float open = juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 48.0f);
                const float mask = open * open * (3.0f - 2.0f * open);
                work[(size_t) k] *= 1.0f - amt + amt * mask;
            }
            break;
        }

        case CrossMode::imprint:
        {
            // B's harmonic structure, coloured by A's spectral envelope
            boxSmooth (work.data(), envA.data(), p.envelopeWidth, prefix);
            boxSmooth (magB.data(), envB.data(), p.envelopeWidth, prefix);

            for (int k = 0; k < nb; ++k)
            {
                const float ratio  = juce::jlimit (0.0f, 1000.0f, (envA[(size_t) k] + eps) / (envB[(size_t) k] + eps));
                const float target = magB[(size_t) k] * ratio;
                const float ma     = work[(size_t) k];

                work[(size_t) k] = (1.0f - amt) * ma + amt * target;

                if (amt * target > (1.0f - amt) * ma)
                    freqA[(size_t) k] = freqB[(size_t) k];
            }
            break;
        }

        case CrossMode::numModes:
        default:
            break;
    }
}

void SpectralVoice::applySculpting()
{
    const auto& p = *params;

    // Spectral gate: remove everything quieter than gateDb below the frame's peak
    if (p.gateDb > -119.0f)
    {
        const float frameMax  = juce::FloatVectorOperations::findMaximum (work.data(), nb);
        const float threshold = frameMax * juce::Decibels::decibelsToGain (p.gateDb);

        for (auto& m : work)
            if (m < threshold)
                m = 0.0f;
    }

    // Tonal / noise separation around spectral peaks
    if (std::abs (p.tonal) > 1.0e-3f)
    {
        auto& isPeak = envA; // scratch

        std::fill (isPeak.begin(), isPeak.end(), 0.0f);

        for (int k = 1; k < nb - 1; ++k)
        {
            if (work[(size_t) k] > 0.0f && work[(size_t) k] >= work[(size_t) k - 1] && work[(size_t) k] > work[(size_t) k + 1])
            {
                for (int j = juce::jmax (0, k - 1); j <= juce::jmin (nb - 1, k + 1); ++j)
                    isPeak[(size_t) j] = 1.0f;
            }
        }

        const float amount = std::abs (p.tonal);
        const bool keepPeaks = p.tonal > 0.0f;

        for (int k = 0; k < nb; ++k)
        {
            const bool peak = isPeak[(size_t) k] > 0.5f;

            if (peak != keepPeaks)
                work[(size_t) k] *= 1.0f - amount;
        }
    }

    // Frequency smear
    const int radius = juce::roundToInt (p.smear * p.smear * 48.0f);

    if (radius > 0)
    {
        boxSmooth (work.data(), envB.data(), radius, prefix);
        std::copy (envB.begin(), envB.end(), work.begin());
    }

    // Spectral tilt around 1 kHz
    if (std::abs (p.tiltDb) > 0.01f)
    {
        const float exponent = p.tiltDb / 6.0206f;

        for (int k = 1; k < nb; ++k)
            work[(size_t) k] *= std::pow ((float) k * Stft::binHz() / 1000.0f, exponent);

        work[0] = 0.0f;
    }
}

void SpectralVoice::remapAndSynthesise()
{
    const auto& p = *params;

    const float semis = (p.keyTrack ? (float) midiNote - p.rootNote : 0.0f)
                        + p.fineSemitones + bendSemitones + p.extraSemitones;
    const float ratio      = std::pow (2.0f, semis / 12.0f);
    const float hostBinHz  = (float) (hostRate / N);
    const float nyquist    = (float) hostRate * 0.49f;
    const bool  warping    = std::abs (p.warp - 1.0f) > 1.0e-4f;

    const auto transform = [&] (float f)
    {
        if (warping && f > 0.0f)
            f = 440.0f * std::pow (f / 440.0f, p.warp);

        return f * ratio + p.shiftHz;
    };

    std::fill (outMag.begin(),  outMag.end(),  0.0f);
    std::fill (outBest.begin(), outBest.end(), 0.0f);

    for (int k = 1; k < nb - 1; ++k)
    {
        const float m = work[(size_t) k];

        if (m <= 0.0f)
            continue;

        const float centre = transform ((float) k * Stft::binHz());

        if (centre <= 0.0f || centre >= nyquist)
            continue;

        const int j = juce::roundToInt (centre / hostBinHz);

        if (j < 1 || j >= nb - 1)
            continue;

        outMag[(size_t) j] += m;

        if (m > outBest[(size_t) j])
        {
            outBest[(size_t) j] = m;
            outFreq[(size_t) j] = transform (freqA[(size_t) k]);
        }
    }

    // Phase vocoder with identity phase locking (Laroche & Dolson): each spectral
    // peak advances by its own partial frequency, and the bins around it are locked
    // to it with the alternating-sign relationship a centred Hann window produces.
    // This keeps every partial's main lobe coherent and avoids "phasiness".
    const float phasePerHz = twoPi * (float) hop / (float) hostRate;
    const float randomness = p.phaseRandom * p.phaseRandom * twoPi;
    constexpr float pi = juce::MathConstants<float>::pi;

    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);

    int lastPeak = -1;
    int regionStart = 1;

    const auto lockRegion = [&] (int peak, int from, int to)
    {
        const float peakPhase = phase[(size_t) peak];

        for (int j = from; j < to; ++j)
        {
            if (j != peak)
                phase[(size_t) j] = peakPhase + pi * (float) ((j - peak) & 1);

            float ph = phase[(size_t) j];

            if (randomness > 0.0f)
                ph += (random.nextFloat() - 0.5f) * randomness;

            const float m = outMag[(size_t) j];
            fftBuffer[(size_t) (2 * j)]     = m * std::cos (ph);
            fftBuffer[(size_t) (2 * j + 1)] = m * std::sin (ph);
        }
    };

    for (int j = 1; j < nb - 1; ++j)
    {
        const float m = outMag[(size_t) j];

        if (m <= 0.0f || m < outMag[(size_t) j - 1] || m <= outMag[(size_t) j + 1])
            continue;

        // j is a peak: advance it, then lock the region shared with the previous peak
        phase[(size_t) j] = wrapPhase (phase[(size_t) j] + outFreq[(size_t) j] * phasePerHz);

        if (lastPeak >= 0)
        {
            const int boundary = (lastPeak + j + 1) / 2;
            lockRegion (lastPeak, regionStart, boundary);
            regionStart = boundary;
        }

        lastPeak = j;
    }

    if (lastPeak >= 0)
        lockRegion (lastPeak, regionStart, nb - 1);

    fft.performRealOnlyInverseTransform (fftBuffer.data());

    // Window, overlap-add and emit one hop of output
    const float scale = 1.0f / overlapGain;

    for (int i = 0; i < N; ++i)
        ola[(size_t) i] += fftBuffer[(size_t) i] * window[(size_t) i] * scale;

    std::copy (ola.begin(), ola.begin() + hop, ready.begin());
    std::move (ola.begin() + hop, ola.end(), ola.begin());
    std::fill (ola.end() - hop, ola.end(), 0.0f);
}

} // namespace sw

/*
  ==============================================================================

   Detail Clipper: a detail preserving hard clipper.

   The DSP is shared with examples/Plugins/DetailClipperPluginDemo.h.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace detail_clipper
{

using namespace juce;

namespace DetailClip
{

//==============================================================================
/** Odd, piecewise hard-clip curve with an optional quadratic knee.

    For a = |x|, threshold t and knee half-width w:
        a <= t - w            : y = a
        t - w < a <= t + w    : y = a - (a - t + w)^2 / (4w)
        a >  t + w            : y = t

    The curve is C1-continuous whenever w > 0 and degenerates to a pure hard
    clip when w == 0. G is its antiderivative, used for ADAA.
*/
struct ClipCurve
{
    float threshold = 1.0f;
    float knee      = 0.0f;   // half-width of the knee region, in linear units

    void set (float newThreshold, float kneeAmount01) noexcept
    {
        threshold = jmax (1.0e-6f, newThreshold);
        knee      = jlimit (0.0f, 1.0f, kneeAmount01) * 0.5f * threshold;
    }

    float process (float x) const noexcept
    {
        const auto a = std::abs (x);
        const auto t = threshold, w = knee;

        float y;

        if (a <= t - w)       y = a;
        else if (a >= t + w)  y = t;
        else                  { const auto d = a - t + w; y = a - d * d / (4.0f * w); }

        return std::copysign (y, x);
    }

    /** Antiderivative (even function, G(0) == 0). Evaluated in double precision
        because ADAA divides differences of G by small input differences. */
    double antiderivative (double x) const noexcept
    {
        const auto a = std::abs (x);
        const auto t = (double) threshold, w = (double) knee;

        if (a <= t - w)
            return 0.5 * a * a;

        if (a <= t + w)
        {
            const auto d = a - t + w;
            return 0.5 * a * a - d * d * d / (12.0 * w);
        }

        const auto top = t + w;
        return 0.5 * top * top - (2.0 / 3.0) * w * w + t * (a - top);
    }
};

//==============================================================================
/** Second-order TPT state variable high-pass (Butterworth). */
struct HighPassSVF
{
    float g = 0.0f, k = MathConstants<float>::sqrt2, h = 1.0f;
    float s1 = 0.0f, s2 = 0.0f;

    void setCutoff (float frequency, float sampleRate) noexcept
    {
        const auto fc = jlimit (10.0f, 0.45f * sampleRate, frequency);
        g = std::tan (MathConstants<float>::pi * fc / sampleRate);
        h = 1.0f / (1.0f + g * (g + k));
    }

    void reset() noexcept { s1 = s2 = 0.0f; }

    float process (float x) noexcept
    {
        const auto hp = (x - (k + g) * s1 - s2) * h;
        const auto v1 = g * hp;
        const auto bp = v1 + s1;
        s1 = bp + v1;
        const auto v2 = g * bp;
        const auto lp = v2 + s2;
        s2 = lp + v2;
        return hp;
    }
};

//==============================================================================
/** One-pole DC blocker, needed when asymmetric clipping introduces an offset. */
struct DCBlocker
{
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;

    void prepare (float sampleRate) noexcept { r = std::exp (-MathConstants<float>::twoPi * 5.0f / sampleRate); }
    void reset() noexcept                    { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const auto y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

//==============================================================================
struct ChannelState
{
    float       previousInput = 0.0f;   // ADAA memory
    HighPassSVF detailFilter;
    DCBlocker   dcBlocker;

    void reset() noexcept
    {
        previousInput = 0.0f;
        detailFilter.reset();
        dcBlocker.reset();
    }
};

/** Everything the per-sample clipper needs, already in linear units. */
struct SampleParams
{
    ClipCurve curve;
    float drive          = 1.0f;
    float bias           = 0.0f;
    float biasOffset     = 0.0f;   // curve.process (bias): removed so silence stays silent
    float detail         = 0.0f;
    float dcBlockAmount  = 0.0f;   // 0 = DC blocker bypassed (keeps the dry path phase-exact)
    float headroomLimit  = 1.0f;   // absolute output limit (ceiling * headroom)
    bool  useADAA        = true;
};

/** Clips one sample for one channel and returns the result (before the
    headroom limiter, which runs after the optional mid/side decode). */
inline float clipSample (float input, ChannelState& state, const SampleParams& p) noexcept
{
    const auto u = input * p.drive + p.bias;

    float clipped, aligned;

    if (p.useADAA)
    {
        const auto u1   = state.previousInput;
        const auto diff = (double) u - (double) u1;
        aligned = 0.5f * (u + u1);   // ADAA output sits half a sample late

        if (std::abs (diff) > 1.0e-5)
            clipped = (float) ((p.curve.antiderivative (u) - p.curve.antiderivative (u1)) / diff);
        else
            clipped = p.curve.process (aligned);

        state.previousInput = u;
    }
    else
    {
        aligned = u;
        clipped = p.curve.process (u);
    }

    // the part of the signal that the clipper removed
    const auto residual = aligned - clipped;

    // give back its high frequency content
    const auto restored = p.detail * state.detailFilter.process (residual);

    const auto out     = clipped - p.biasOffset + restored;
    const auto blocked = state.dcBlocker.process (out);   // always run, so its state stays warm
    return out + p.dcBlockAmount * (blocked - out);
}

} // namespace DetailClip

//==============================================================================
namespace ParamIDs
{
    #define DETAIL_CLIP_PARAM_ID(str) constexpr const char* str { #str };

    DETAIL_CLIP_PARAM_ID (drive)
    DETAIL_CLIP_PARAM_ID (ceiling)
    DETAIL_CLIP_PARAM_ID (knee)
    DETAIL_CLIP_PARAM_ID (asymmetry)
    DETAIL_CLIP_PARAM_ID (detail)
    DETAIL_CLIP_PARAM_ID (detailFreq)
    DETAIL_CLIP_PARAM_ID (detailHeadroom)
    DETAIL_CLIP_PARAM_ID (oversampling)
    DETAIL_CLIP_PARAM_ID (adaa)
    DETAIL_CLIP_PARAM_ID (trueCeiling)
    DETAIL_CLIP_PARAM_ID (stereoMode)
    DETAIL_CLIP_PARAM_ID (autoGain)
    DETAIL_CLIP_PARAM_ID (outputGain)
    DETAIL_CLIP_PARAM_ID (mix)
    DETAIL_CLIP_PARAM_ID (delta)

    #undef DETAIL_CLIP_PARAM_ID
}

//==============================================================================
class DetailClipperAudioProcessor final : public AudioProcessor
{
public:
    //==============================================================================
    DetailClipperAudioProcessor()
        : AudioProcessor (BusesProperties().withInput  ("Input",  AudioChannelSet::stereo())
                                           .withOutput ("Output", AudioChannelSet::stereo())),
          apvts (*this, nullptr, "DetailClipper", createLayout())
    {
        auto getFloat = [this] (const char* id) { return dynamic_cast<AudioParameterFloat*>  (apvts.getParameter (id)); };
        auto getBool  = [this] (const char* id) { return dynamic_cast<AudioParameterBool*>   (apvts.getParameter (id)); };
        auto getEnum  = [this] (const char* id) { return dynamic_cast<AudioParameterChoice*> (apvts.getParameter (id)); };

        drive          = getFloat (ParamIDs::drive);
        ceiling        = getFloat (ParamIDs::ceiling);
        knee           = getFloat (ParamIDs::knee);
        asymmetry      = getFloat (ParamIDs::asymmetry);
        detail         = getFloat (ParamIDs::detail);
        detailFreq     = getFloat (ParamIDs::detailFreq);
        detailHeadroom = getFloat (ParamIDs::detailHeadroom);
        outputGain     = getFloat (ParamIDs::outputGain);
        mix            = getFloat (ParamIDs::mix);
        oversampling   = getEnum  (ParamIDs::oversampling);
        stereoMode     = getEnum  (ParamIDs::stereoMode);
        adaa           = getBool  (ParamIDs::adaa);
        trueCeiling    = getBool  (ParamIDs::trueCeiling);
        autoGain       = getBool  (ParamIDs::autoGain);
        delta          = getBool  (ParamIDs::delta);

        jassert (drive != nullptr && ceiling != nullptr && knee != nullptr && asymmetry != nullptr
                 && detail != nullptr && detailFreq != nullptr && detailHeadroom != nullptr
                 && outputGain != nullptr && mix != nullptr && oversampling != nullptr
                 && stereoMode != nullptr && adaa != nullptr && trueCeiling != nullptr && autoGain != nullptr && delta != nullptr);
    }

    //==============================================================================
    static AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using APF = AudioParameterFloat;
        using Attr = AudioParameterFloatAttributes;

        auto dbAttr = [] { return Attr().withLabel ("dB")
                                        .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; }); };
        auto pcAttr = [] { return Attr().withLabel ("%")
                                        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; }); };
        auto hzAttr = [] { return Attr().withLabel ("Hz")
                                        .withStringFromValueFunction ([] (float v, int)
                                                                      {
                                                                          return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz"
                                                                                              : String (roundToInt (v)) + " Hz";
                                                                      }); };

        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<APF> (ParameterID { ParamIDs::drive, 1 }, "Drive",
                                           NormalisableRange<float> (0.0f, 36.0f, 0.01f), 6.0f, dbAttr()));
        layout.add (std::make_unique<APF> (ParameterID { ParamIDs::ceiling, 1 }, "Ceiling",
                                           NormalisableRange<float> (-30.0f, 0.0f, 0.01f), -0.3f, dbAttr()));
        layout.add (std::make_unique<APF> (ParameterID { ParamIDs::knee, 1 }, "Knee",
                                           NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pcAttr()));
        layout.add (std::make_unique<APF> (ParameterID { ParamIDs::asymmetry, 1 }, "Asymmetry",
                                           NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, pcAttr()));
        layout.add (std::make_unique<APF> (ParameterID { ParamIDs::detail, 1 }, "Detail",
                                           NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pcAttr()));
        layout.add (std::make_unique<APF> (ParameterID { ParamIDs::detailFreq, 1 }, "Detail Freq",
                                           NormalisableRange<float> (200.0f, 16000.0f, 1.0f, 0.3f), 2500.0f, hzAttr()));
        layout.add (std::make_unique<APF> (ParameterID { ParamIDs::detailHeadroom, 1 }, "Detail Headroom",
                                           NormalisableRange<float> (0.0f, 12.0f, 0.01f), 3.0f, dbAttr()));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::oversampling, 1 }, "Oversampling",
                                                            StringArray { "1x", "2x", "4x", "8x", "16x" }, 2));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::adaa, 1 }, "Anti-Alias (ADAA)", true));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::trueCeiling, 1 }, "True Ceiling", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::stereoMode, 1 }, "Stereo Mode",
                                                            StringArray { "Left/Right", "Mid/Side" }, 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::autoGain, 1 }, "Auto Gain", false));
        layout.add (std::make_unique<APF> (ParameterID { ParamIDs::outputGain, 1 }, "Output",
                                           NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, dbAttr()));
        layout.add (std::make_unique<APF> (ParameterID { ParamIDs::mix, 1 }, "Mix",
                                           NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pcAttr()));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::delta, 1 }, "Delta (Listen Removed)", false));

        return layout;
    }

    //==============================================================================
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& in  = layouts.getMainInputChannelSet();
        const auto& out = layouts.getMainOutputChannelSet();

        return in == out && (in == AudioChannelSet::mono() || in == AudioChannelSet::stereo());
    }

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        baseSampleRate = sampleRate;
        maxBlockSize   = jmax (1, samplesPerBlock);

        const auto numChannels = (size_t) jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());

        for (size_t i = 0; i < oversamplers.size(); ++i)
        {
            oversamplers[i] = std::make_unique<dsp::Oversampling<float>> (numChannels, i,
                                                                          dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                          true, true);
            oversamplers[i]->initProcessing ((size_t) maxBlockSize);
        }

        dryBuffer.setSize ((int) numChannels, maxBlockSize);
        scopeTemp.setSize (2, maxBlockSize);

        dryDelay.prepare ({ sampleRate, (uint32) maxBlockSize, (uint32) numChannels });
        dryDelay.setMaximumDelayInSamples (1 << 12);

        outputSmoother.reset (sampleRate, 0.05);
        mixSmoother   .reset (sampleRate, 0.05);
        outputSmoother.setCurrentAndTargetValue (computeOutputGain());
        mixSmoother   .setCurrentAndTargetValue (mix->get() * 0.01f);

        activeFactor = -1;   // forces configureOversampling() to set everything up
        configureOversampling (oversampling->getIndex());
    }

    void releaseResources() override {}

    //==============================================================================
    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        ScopedNoDenormals noDenormals;

        const auto numChannels = getTotalNumInputChannels();
        const auto numSamples  = buffer.getNumSamples();

        for (auto ch = numChannels; ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);

        if (numSamples == 0 || numChannels == 0)
            return;

        // The host should never exceed the block size given in prepareToPlay,
        // but split the block just in case.
        if (numSamples > maxBlockSize)
        {
            for (int start = 0; start < numSamples; start += maxBlockSize)
            {
                const auto len = jmin (maxBlockSize, numSamples - start);
                AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
                MidiBuffer dummy;
                processBlock (sub, dummy);
            }

            return;
        }

        configureOversampling (oversampling->getIndex());
        updateDryDelay();

        // keep a copy of the input for the dry / delta paths
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        const auto inputPeak = buffer.getMagnitude (0, numSamples);

        // ---- oversampled clipping ------------------------------------------
        dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numChannels, (size_t) numSamples);
        auto& os = *oversamplers[(size_t) activeFactor];
        auto upBlock = os.processSamplesUp (block);

        processOversampled (upBlock);

        os.processSamplesDown (block);

        // The anti-imaging filters ring a little (Gibbs overshoot) around the
        // flat tops, so the downsampled signal can exceed the limit by up to
        // about a dB. Optionally catch that at the base rate.
        if (trueCeiling->get())
        {
            const auto limit = Decibels::decibelsToGain (ceiling->get() + detailHeadroom->get());

            for (int ch = 0; ch < numChannels; ++ch)
                FloatVectorOperations::clip (buffer.getWritePointer (ch), buffer.getReadPointer (ch), -limit, limit, numSamples);
        }

        // ---- dry alignment, delta, mix, output gain ------------------------
        const auto isDelta      = delta->get();
        const auto driveGainEnd = Decibels::decibelsToGain (drive->get());

        outputSmoother.setTargetValue (computeOutputGain());
        mixSmoother   .setTargetValue (mix->get() * 0.01f);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto outGain = outputSmoother.getNextValue();
            const auto wetMix  = mixSmoother.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                dryDelay.pushSample (ch, dryBuffer.getSample (ch, i));
                const auto dry = dryDelay.popSample (ch);
                auto* data = buffer.getWritePointer (ch);
                const auto wet = data[i];

                if (ch == 0)
                {
                    scopeTemp.setSample (0, i, dry * driveGainEnd);
                    scopeTemp.setSample (1, i, wet);
                }

                if (isDelta)
                {
                    // what the clipper took away, at the driven level
                    data[i] = (dry * driveGainEnd - wet) * outGain;
                }
                else
                {
                    data[i] = (dry + wetMix * (wet * outGain - dry));
                }
            }
        }

        // ---- scope feed (drops samples while nobody is reading) -------------
        {
            const auto scope = scopeFifo.write (numSamples);
            auto copy = [&] (int start, int size, int offset)
            {
                if (size > 0)
                {
                    FloatVectorOperations::copy (scopeDriven.data() + start, scopeTemp.getReadPointer (0, offset), size);
                    FloatVectorOperations::copy (scopeOutput.data() + start, scopeTemp.getReadPointer (1, offset), size);
                }
            };
            copy (scope.startIndex1, scope.blockSize1, 0);
            copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
        }

        // ---- metering --------------------------------------------------------
        inputPeakMeter .store (jmax (inputPeak, inputPeakMeter.load() * 0.9f));
        outputPeakMeter.store (jmax (buffer.getMagnitude (0, numSamples), outputPeakMeter.load() * 0.9f));
        clipAmountMeter.store (jmax (blockClipAmount, clipAmountMeter.load() * 0.9f));
    }

    using AudioProcessor::processBlock;

    //==============================================================================
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    //==============================================================================
    const String getName() const override                  { return "Detail Clipper"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    //==============================================================================
    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const String getProgramName (int) override             { return "None"; }
    void changeProgramName (int, const String&) override   {}

    //==============================================================================
    void getStateInformation (MemoryBlock& destData) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, destData);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (ValueTree::fromXml (*xml));
    }

    //==============================================================================
    /** Builds the static curve for the GUI (input gain -> output, no detail). */
    DetailClip::SampleParams getDisplayParams() const
    {
        return makeParams (Decibels::decibelsToGain (drive->get()),
                           Decibels::decibelsToGain (ceiling->get()),
                           knee->get() * 0.01f,
                           asymmetry->get() * 0.01f,
                           0.0f,
                           Decibels::decibelsToGain (detailHeadroom->get()),
                           false);
    }

    AudioProcessorValueTreeState apvts;

    /** Scope feed for the editor (single producer: audio thread, single consumer: editor timer). */
    static constexpr int scopeCapacity = 8192;
    AbstractFifo scopeFifo { scopeCapacity };
    std::array<float, scopeCapacity> scopeDriven {}, scopeOutput {};

    std::atomic<float> inputPeakMeter  { 0.0f };
    std::atomic<float> outputPeakMeter { 0.0f };
    std::atomic<float> clipAmountMeter { 0.0f };   // peak overshoot above ceiling, linear ratio - 1

private:
    //==============================================================================
    static DetailClip::SampleParams makeParams (float driveGain, float ceilingGain, float knee01,
                                                float asym, float detail01, float headroomGain, bool useADAA)
    {
        DetailClip::SampleParams p;
        p.drive  = driveGain;
        p.curve.set (ceilingGain, knee01);
        p.bias   = asym * 0.5f * ceilingGain;
        p.biasOffset    = p.curve.process (p.bias);
        p.dcBlockAmount = jmin (1.0f, std::abs (asym) * 20.0f);
        p.detail        = detail01;
        p.headroomLimit = ceilingGain * headroomGain;
        p.useADAA       = useADAA;
        return p;
    }

    float computeOutputGain() const
    {
        auto gainDb = outputGain->get();

        if (autoGain->get())
            gainDb -= drive->get();

        return Decibels::decibelsToGain (gainDb);
    }

    void configureOversampling (int newFactor)
    {
        newFactor = jlimit (0, (int) oversamplers.size() - 1, newFactor);

        if (newFactor == activeFactor || oversamplers[(size_t) newFactor] == nullptr)
            return;

        activeFactor = newFactor;
        auto& os = *oversamplers[(size_t) activeFactor];
        os.reset();

        oversamplerLatency = (int) std::round (os.getLatencyInSamples());
        dryDelay.reset();
        activeADAA = ! adaa->get();   // forces updateDryDelay() to run
        updateDryDelay();

        const auto osRate = (float) (baseSampleRate * (double) (1 << activeFactor));
        oversampledRate = osRate;

        for (auto& s : channelStates)
        {
            s.reset();
            s.dcBlocker.prepare (osRate);
        }

        for (auto* sm : { &driveSmoother, &ceilingSmoother, &headroomSmoother, &detailFreqSmoother })
            sm->reset ((double) osRate, 0.03);

        for (auto* sm : { &kneeSmoother, &asymSmoother, &detailSmoother })
            sm->reset ((double) osRate, 0.03);

        driveSmoother     .setCurrentAndTargetValue (Decibels::decibelsToGain (drive->get()));
        ceilingSmoother   .setCurrentAndTargetValue (Decibels::decibelsToGain (ceiling->get()));
        kneeSmoother      .setCurrentAndTargetValue (knee->get() * 0.01f);
        asymSmoother      .setCurrentAndTargetValue (asymmetry->get() * 0.01f);
        detailSmoother    .setCurrentAndTargetValue (detail->get() * 0.01f);
        headroomSmoother  .setCurrentAndTargetValue (Decibels::decibelsToGain (detailHeadroom->get()));
        detailFreqSmoother.setCurrentAndTargetValue (detailFreq->get());
    }

    /** ADAA delays the wet path by half an oversampled sample, so the dry path is
        delayed by the same (fractional) amount to keep Mix and Delta phase-exact. */
    void updateDryDelay()
    {
        const auto useADAA = adaa->get();

        if (useADAA == activeADAA)
            return;

        activeADAA = useADAA;
        const auto adaaDelay = useADAA ? 0.5f / (float) (1 << activeFactor) : 0.0f;
        dryDelay.setDelay ((float) oversamplerLatency + adaaDelay);
        setLatencySamples (oversamplerLatency + (adaaDelay > 0.25f ? 1 : 0));
    }

    void processOversampled (dsp::AudioBlock<float>& block)
    {
        const auto numChannels = jmin ((int) block.getNumChannels(), (int) channelStates.size());
        const auto numSamples  = (int) block.getNumSamples();
        const auto midSide     = stereoMode->getIndex() == 1 && numChannels == 2;
        const auto useADAA     = adaa->get();

        driveSmoother     .setTargetValue (Decibels::decibelsToGain (drive->get()));
        ceilingSmoother   .setTargetValue (Decibels::decibelsToGain (ceiling->get()));
        kneeSmoother      .setTargetValue (knee->get() * 0.01f);
        asymSmoother      .setTargetValue (asymmetry->get() * 0.01f);
        detailSmoother    .setTargetValue (detail->get() * 0.01f);
        headroomSmoother  .setTargetValue (Decibels::decibelsToGain (detailHeadroom->get()));
        detailFreqSmoother.setTargetValue (detailFreq->get());

        // the crossover moves slowly enough to update once per block
        const auto cutoff = detailFreqSmoother.skip (numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
            channelStates[(size_t) ch].detailFilter.setCutoff (cutoff, oversampledRate);

        float* data[2] = { block.getChannelPointer (0),
                           numChannels > 1 ? block.getChannelPointer (1) : nullptr };

        auto maxDriven = 0.0f, ceilingNow = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto params = makeParams (driveSmoother.getNextValue(),
                                            ceilingSmoother.getNextValue(),
                                            kneeSmoother.getNextValue(),
                                            asymSmoother.getNextValue(),
                                            detailSmoother.getNextValue(),
                                            headroomSmoother.getNextValue(),
                                            useADAA);
            ceilingNow = params.curve.threshold;

            float frame[2] = { data[0][i], numChannels > 1 ? data[1][i] : 0.0f };

            if (midSide)
            {
                const auto m = 0.5f * (frame[0] + frame[1]);
                const auto s = 0.5f * (frame[0] - frame[1]);
                frame[0] = m;
                frame[1] = s;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                maxDriven = jmax (maxDriven, std::abs (frame[ch] * params.drive + params.bias));
                frame[ch] = DetailClip::clipSample (frame[ch], channelStates[(size_t) ch], params);
            }

            if (midSide)
            {
                const auto l = frame[0] + frame[1];
                const auto r = frame[0] - frame[1];
                frame[0] = l;
                frame[1] = r;
            }

            // limit how far the restored detail (and M/S decoding) may exceed the ceiling
            for (int ch = 0; ch < numChannels; ++ch)
                data[ch][i] = jlimit (-params.headroomLimit, params.headroomLimit, frame[ch]);
        }

        blockClipAmount = jmax (0.0f, maxDriven / ceilingNow - 1.0f);
    }

    //==============================================================================
    AudioParameterFloat*  drive          = nullptr;
    AudioParameterFloat*  ceiling        = nullptr;
    AudioParameterFloat*  knee           = nullptr;
    AudioParameterFloat*  asymmetry      = nullptr;
    AudioParameterFloat*  detail         = nullptr;
    AudioParameterFloat*  detailFreq     = nullptr;
    AudioParameterFloat*  detailHeadroom = nullptr;
    AudioParameterFloat*  outputGain     = nullptr;
    AudioParameterFloat*  mix            = nullptr;
    AudioParameterChoice* oversampling   = nullptr;
    AudioParameterChoice* stereoMode     = nullptr;
    AudioParameterBool*   adaa           = nullptr;
    AudioParameterBool*   trueCeiling    = nullptr;
    AudioParameterBool*   autoGain       = nullptr;
    AudioParameterBool*   delta          = nullptr;

    std::array<std::unique_ptr<dsp::Oversampling<float>>, 5> oversamplers;
    std::array<DetailClip::ChannelState, 2> channelStates;

    dsp::DelayLine<float, dsp::DelayLineInterpolationTypes::Lagrange3rd> dryDelay;
    AudioBuffer<float> dryBuffer, scopeTemp;

    SmoothedValue<float, ValueSmoothingTypes::Multiplicative> driveSmoother { 1.0f }, ceilingSmoother { 1.0f },
                                                              headroomSmoother { 1.0f }, detailFreqSmoother { 1000.0f };
    SmoothedValue<float> kneeSmoother, asymSmoother, detailSmoother, outputSmoother, mixSmoother;

    double baseSampleRate  = 44100.0;
    float  oversampledRate = 44100.0f;
    int    maxBlockSize    = 512;
    int    activeFactor    = -1;
    int    oversamplerLatency = 0;
    bool   activeADAA      = false;
    float  blockClipAmount = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DetailClipperAudioProcessor)
};

} // namespace detail_clipper

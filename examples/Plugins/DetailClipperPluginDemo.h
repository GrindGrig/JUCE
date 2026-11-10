/*
  ==============================================================================

   This file is part of the JUCE framework examples.
   Copyright (c) Raw Material Software Limited

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
   REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
   AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
   INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
   LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
   OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
   PERFORMANCE OF THIS SOFTWARE.

  ==============================================================================
*/

/*******************************************************************************
 The block below describes the properties of this PIP. A PIP is a short snippet
 of code that can be read by the Projucer and used to generate a JUCE project.

 BEGIN_JUCE_PIP_METADATA

 name:             DetailClipperPlugin
 version:          1.0.0
 vendor:           JUCE
 website:          http://juce.com
 description:      Detail preserving hard clipper with fine tuning controls.

 dependencies:     juce_audio_basics, juce_audio_devices, juce_audio_formats,
                   juce_audio_plugin_client, juce_audio_processors,
                   juce_audio_utils, juce_core, juce_data_structures, juce_dsp,
                   juce_events, juce_graphics, juce_gui_basics, juce_gui_extra,
                   juce_audio_processors_headless
 exporters:        xcode_mac, vs2022, vs2026, linux_make

 moduleFlags:      JUCE_STRICT_REFCOUNTEDPOINTER=1

 type:             AudioProcessor
 mainClass:        DetailClipperAudioProcessor

 useLocalCopy:     1

 END_JUCE_PIP_METADATA

*******************************************************************************/

#pragma once

/*  How this clipper preserves detail
    ---------------------------------

    A plain hard clipper flattens everything above the ceiling. Most of the
    energy that crosses the ceiling is low frequency "body" (kick, bass, the
    fundamental of a vocal), but the flat top also wipes out the transient edges
    and high frequency content riding on those peaks, and the sharp corners
    alias badly. This processor attacks both problems:

    1.  Anti-aliasing: the clipper runs at up to 16x oversampling, and the
        clipping curve itself is evaluated with first-order antiderivative
        anti-aliasing (ADAA). Together these keep the added harmonics from
        folding back down as inharmonic grit.

    2.  Detail restoration: the "residual" (input minus clipped output, i.e. the
        part the clipper removed) is high-passed and a controllable amount of it
        is mixed back in. At 100 % detail the output is

            input - lowpass (residual)

        so only the low frequency part of each over is actually clipped, while
        transients and air pass through. A "Detail Headroom" control sets how
        far restored detail may poke above the ceiling (0 dB = strict ceiling).

    3.  Fine tuning: knee (pure hard -> rounded corner), asymmetry (even
        harmonics), mid/side clipping (keeps the stereo image stable), auto gain,
        a latency-compensated dry/wet mix and a "delta" monitor that lets you
        listen to exactly what is being removed.
*/

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
    AudioBuffer<float> dryBuffer;

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

//==============================================================================
class DetailClipperEditor final : public AudioProcessorEditor,
                                  private Timer
{
public:
    explicit DetailClipperEditor (DetailClipperAudioProcessor& p)
        : AudioProcessorEditor (p), processor (p)
    {
        addKnob (driveKnob,      ParamIDs::drive,          "Drive");
        addKnob (ceilingKnob,    ParamIDs::ceiling,        "Ceiling");
        addKnob (kneeKnob,       ParamIDs::knee,           "Knee");
        addKnob (asymKnob,       ParamIDs::asymmetry,      "Asymmetry");
        addKnob (detailKnob,     ParamIDs::detail,         "Detail");
        addKnob (detailFreqKnob, ParamIDs::detailFreq,     "Detail Freq");
        addKnob (headroomKnob,   ParamIDs::detailHeadroom, "Headroom");
        addKnob (outputKnob,     ParamIDs::outputGain,     "Output");
        addKnob (mixKnob,        ParamIDs::mix,            "Mix");

        addCombo (oversamplingBox, ParamIDs::oversampling, "Oversampling");
        addCombo (stereoModeBox,   ParamIDs::stereoMode,   "Stereo");

        addToggle (adaaButton,     ParamIDs::adaa,     "ADAA");
        addToggle (trueCeilButton, ParamIDs::trueCeiling, "True Ceiling");
        addToggle (autoGainButton, ParamIDs::autoGain, "Auto Gain");
        addToggle (deltaButton,    ParamIDs::delta,    "Delta");

        setResizable (true, true);
        setResizeLimits (640, 420, 1400, 900);
        setSize (820, 500);

        startTimerHz (30);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff15171c));

        auto header = getLocalBounds().removeFromTop (36).reduced (12, 0);
        g.setColour (Colours::white);
        g.setFont (FontOptions (20.0f, Font::bold));
        g.drawText ("DETAIL CLIPPER", header, Justification::centredLeft);

        g.setFont (FontOptions (13.0f));
        g.setColour (Colours::white.withAlpha (0.7f));
        const auto clipDb = Decibels::gainToDecibels (1.0f + displayedClip, -60.0f);
        g.drawText ("Clipping: " + String (clipDb, 1) + " dB   |   In: "
                        + String (Decibels::gainToDecibels (displayedIn, -100.0f), 1) + " dBFS   Out: "
                        + String (Decibels::gainToDecibels (displayedOut, -100.0f), 1) + " dBFS",
                    header, Justification::centredRight);

        drawTransferCurve (g, curveArea.toFloat());
        drawClipMeter (g, meterArea.toFloat());

        g.setColour (Colours::white.withAlpha (0.45f));
        g.setFont (FontOptions (12.0f));

        for (auto& [area, text] : sectionLabels)
            g.drawText (text, area, Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        area.removeFromTop (30);

        auto top = area.removeFromTop (area.getHeight() / 2);
        meterArea = top.removeFromRight (28).reduced (0, 4);
        top.removeFromRight (8);
        curveArea = top.reduced (0, 4);

        area.removeFromTop (6);
        sectionLabels.clear();

        auto controls = area.removeFromTop (area.getHeight() - 34);
        auto bottom   = area;

        const auto knobWidth = controls.getWidth() / 9;

        auto placeSection = [&] (std::initializer_list<Knob*> knobs, const char* title)
        {
            auto section = controls.removeFromLeft (knobWidth * (int) knobs.size());
            sectionLabels.push_back ({ section.removeFromTop (16).reduced (4, 0), title });

            for (auto* k : knobs)
            {
                auto r = section.removeFromLeft (knobWidth);
                k->label.setBounds (r.removeFromBottom (18));
                k->slider.setBounds (r);
            }
        };

        placeSection ({ &driveKnob, &ceilingKnob },              "CLIP");
        placeSection ({ &kneeKnob, &asymKnob },                  "SHAPE");
        placeSection ({ &detailKnob, &detailFreqKnob, &headroomKnob }, "DETAIL");
        placeSection ({ &outputKnob, &mixKnob },                 "OUTPUT");

        bottom.removeFromTop (6);
        const auto w = bottom.getWidth() / 6;
        oversamplingBox.box.setBounds (bottom.removeFromLeft (w).reduced (4, 0));
        stereoModeBox  .box.setBounds (bottom.removeFromLeft (w).reduced (4, 0));
        adaaButton     .button.setBounds (bottom.removeFromLeft (w).reduced (4, 0));
        trueCeilButton .button.setBounds (bottom.removeFromLeft (w).reduced (4, 0));
        autoGainButton .button.setBounds (bottom.removeFromLeft (w).reduced (4, 0));
        deltaButton    .button.setBounds (bottom.removeFromLeft (w).reduced (4, 0));
    }

private:
    //==============================================================================
    struct Knob
    {
        Slider slider { Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow };
        Label  label;
        std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Combo
    {
        ComboBox box;
        std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Toggle
    {
        ToggleButton button;
        std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void addKnob (Knob& k, const char* id, const String& name)
    {
        k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 18);
        k.slider.setColour (Slider::rotarySliderFillColourId, Colour (0xffff7a3d));
        k.slider.setColour (Slider::thumbColourId,            Colour (0xffffc29f));
        k.slider.setPopupDisplayEnabled (false, false, nullptr);
        k.label.setText (name, dontSendNotification);
        k.label.setJustificationType (Justification::centred);
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
        k.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k.slider);
        k.slider.onValueChange = [this] { repaint (curveArea); };
    }

    void addCombo (Combo& c, const char* id, const String& tooltip)
    {
        if (auto* param = dynamic_cast<AudioParameterChoice*> (processor.apvts.getParameter (id)))
            c.box.addItemList (param->choices, 1);

        c.box.setTooltip (tooltip);
        addAndMakeVisible (c.box);
        c.attachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, id, c.box);
    }

    void addToggle (Toggle& t, const char* id, const String& name)
    {
        t.button.setButtonText (name);
        addAndMakeVisible (t.button);
        t.attachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, t.button);
    }

    //==============================================================================
    void timerCallback() override
    {
        displayedIn   = processor.inputPeakMeter.load();
        displayedOut  = processor.outputPeakMeter.load();
        displayedClip = processor.clipAmountMeter.load();
        repaint();
    }

    void drawTransferCurve (Graphics& g, Rectangle<float> r) const
    {
        g.setColour (Colour (0xff1e2128));
        g.fillRoundedRectangle (r, 6.0f);

        const auto p = processor.getDisplayParams();
        constexpr auto range = 1.25f;   // shown input / output range, linear

        auto toScreen = [&] (float x, float y)
        {
            return Point<float> (jmap (x, -range, range, r.getX(), r.getRight()),
                                 jmap (y, -range, range, r.getBottom(), r.getY()));
        };

        // grid
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawLine ({ toScreen (-range, 0.0f), toScreen (range, 0.0f) });
        g.drawLine ({ toScreen (0.0f, -range), toScreen (0.0f, range) });
        g.drawLine ({ toScreen (-range, -range), toScreen (range, range) });

        // ceiling and detail headroom lines
        const auto t = p.curve.threshold;
        g.setColour (Colour (0xffff7a3d).withAlpha (0.35f));
        g.drawLine ({ toScreen (-range, t), toScreen (range, t) }, 1.0f);
        g.drawLine ({ toScreen (-range, -t), toScreen (range, -t) }, 1.0f);

        g.setColour (Colour (0xff6fd3ff).withAlpha (0.25f));
        const auto hl = jmin (p.headroomLimit, range);
        const float dashes[] = { 4.0f, 4.0f };
        g.drawDashedLine ({ toScreen (-range, hl), toScreen (range, hl) }, dashes, 2);
        g.drawDashedLine ({ toScreen (-range, -hl), toScreen (range, -hl) }, dashes, 2);

        // curve
        Path path;
        constexpr int steps = 400;

        for (int i = 0; i <= steps; ++i)
        {
            const auto x = jmap ((float) i, 0.0f, (float) steps, -range, range);
            const auto y = p.curve.process (x * p.drive + p.bias) - p.biasOffset;
            const auto pt = toScreen (x, jlimit (-range, range, y));

            if (i == 0) path.startNewSubPath (pt);
            else        path.lineTo (pt);
        }

        g.setColour (Colour (0xffff7a3d));
        g.strokePath (path, PathStrokeType (2.0f));

        g.setColour (Colours::white.withAlpha (0.4f));
        g.setFont (FontOptions (11.0f));
        g.drawText ("transfer curve (drive + knee + asymmetry)   orange: ceiling   blue: detail headroom",
                    r.reduced (8, 4).toNearestInt(), Justification::bottomLeft);
    }

    void drawClipMeter (Graphics& g, Rectangle<float> r) const
    {
        g.setColour (Colour (0xff1e2128));
        g.fillRoundedRectangle (r, 4.0f);

        // 0 .. 24 dB of clipping
        const auto db = jlimit (0.0f, 24.0f, Decibels::gainToDecibels (1.0f + displayedClip, 0.0f));
        auto fill = r.reduced (3.0f);
        fill = fill.removeFromBottom (fill.getHeight() * db / 24.0f);

        g.setColour (Colour (0xffff7a3d));
        g.fillRoundedRectangle (fill, 2.0f);
    }

    //==============================================================================
    DetailClipperAudioProcessor& processor;

    Knob driveKnob, ceilingKnob, kneeKnob, asymKnob, detailKnob, detailFreqKnob, headroomKnob, outputKnob, mixKnob;
    Combo oversamplingBox, stereoModeBox;
    Toggle adaaButton, trueCeilButton, autoGainButton, deltaButton;

    Rectangle<int> curveArea, meterArea;
    std::vector<std::pair<Rectangle<int>, String>> sectionLabels;

    float displayedIn = 0.0f, displayedOut = 0.0f, displayedClip = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DetailClipperEditor)
};

inline AudioProcessorEditor* DetailClipperAudioProcessor::createEditor()
{
    return new DetailClipperEditor (*this);
}

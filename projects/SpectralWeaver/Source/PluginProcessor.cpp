#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using APVTS = juce::AudioProcessorValueTreeState;

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* id, const char* name,
                                                           juce::NormalisableRange<float> range, float def,
                                                           const char* unit = "", int decimals = 2)
    {
        auto attrs = juce::AudioParameterFloatAttributes()
                         .withLabel (unit)
                         .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); });

        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attrs);
    }

    const char* const allParamIds[] = { "start", "length", "speed", "freeze", "loop", "keyTrack", "root", "tune", "bendRange",
                                        "blur", "smear", "tonal", "tilt", "gate", "warp", "shift", "phaseRand",
                                        "crossMode", "crossAmount", "envWidth", "syncB", "speedB",
                                        "attack", "decay", "sustain", "release", "gain",
                                        "lfoRate", "lfoDepth", "lfoShape", "lfoTarget", "normA", "normB" };

    const char* const resettableGroups[] = { "playback", "cross", "spectral", "lfo", "amp" };
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectralWeaverProcessor::createLayout()
{
    APVTS::ParameterLayout layout;

    auto playback = std::make_unique<juce::AudioProcessorParameterGroup> ("playback", "Playback", "|");
    playback->addChild (floatParam ("start",  "Start",  { 0.0f, 1.0f }, 0.0f),
                        floatParam ("length", "Length", { 0.0f, 1.0f }, 1.0f),
                        floatParam ("speed",  "Speed",  { -4.0f, 4.0f }, 1.0f, "x"),
                        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "freeze", 1 }, "Freeze", false),
                        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "loop", 1 }, "Loop", true),
                        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "keyTrack", 1 }, "Key Track", true),
                        std::make_unique<juce::AudioParameterInt>  (juce::ParameterID { "root", 1 }, "Root Note", 0, 127, 60),
                        floatParam ("tune", "Tune", { -24.0f, 24.0f }, 0.0f, "st"),
                        floatParam ("bendRange", "Bend Range", { 0.0f, 24.0f, 1.0f }, 2.0f, "st", 0));

    auto spectral = std::make_unique<juce::AudioProcessorParameterGroup> ("spectral", "Spectral", "|");
    spectral->addChild (floatParam ("blur",      "Blur",        { 0.0f, 1.0f }, 0.0f),
                        floatParam ("smear",     "Smear",       { 0.0f, 1.0f }, 0.0f),
                        floatParam ("tonal",     "Tonal/Noise", { -1.0f, 1.0f }, 0.0f),
                        floatParam ("tilt",      "Tilt",        { -12.0f, 12.0f }, 0.0f, "dB/oct", 1),
                        floatParam ("gate",      "Gate",        { -120.0f, 0.0f }, -120.0f, "dB", 0),
                        floatParam ("warp",      "Warp",        skewed (0.5f, 2.0f, 1.0f), 1.0f),
                        floatParam ("shift",     "Shift",       { -2000.0f, 2000.0f }, 0.0f, "Hz", 0),
                        floatParam ("phaseRand", "Diffuse",     { 0.0f, 1.0f }, 0.0f));

    auto cross = std::make_unique<juce::AudioProcessorParameterGroup> ("cross", "Cross Modulation", "|");
    cross->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "crossMode", 1 }, "Cross Mode", sw::crossModeNames(), 0),
                     floatParam ("crossAmount", "Cross Amount", { 0.0f, 1.0f }, 0.75f),
                     std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "envWidth", 1 }, "Envelope Width", 1, 64, 8),
                     std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "syncB", 1 }, "Sync B", true),
                     floatParam ("speedB", "B Speed", { -4.0f, 4.0f }, 1.0f, "x"));

    auto amp = std::make_unique<juce::AudioProcessorParameterGroup> ("amp", "Amplifier", "|");
    amp->addChild (floatParam ("attack",  "Attack",  skewed (0.001f, 5.0f, 0.3f), 0.01f, "s", 3),
                   floatParam ("decay",   "Decay",   skewed (0.001f, 5.0f, 0.5f), 0.3f, "s", 3),
                   floatParam ("sustain", "Sustain", { 0.0f, 1.0f }, 1.0f),
                   floatParam ("release", "Release", skewed (0.001f, 10.0f, 1.0f), 0.5f, "s", 3),
                   floatParam ("gain",    "Gain",    { -48.0f, 12.0f }, -6.0f, "dB", 1));

    auto lfo = std::make_unique<juce::AudioProcessorParameterGroup> ("lfo", "LFO", "|");
    lfo->addChild (floatParam ("lfoRate",  "LFO Rate",  skewed (0.01f, 20.0f, 1.0f), 0.5f, "Hz"),
                   floatParam ("lfoDepth", "LFO Depth", { 0.0f, 1.0f }, 0.0f),
                   std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "lfoShape", 1 }, "LFO Shape", lfoShapeNames(), 0),
                   std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "lfoTarget", 1 }, "LFO Target", lfoTargetNames(), 0));

    auto samples = std::make_unique<juce::AudioProcessorParameterGroup> ("samples", "Samples", "|");
    samples->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "normA", 1 }, "Normalize A", false),
                       std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "normB", 1 }, "Normalize B", false));

    layout.add (std::move (playback), std::move (spectral), std::move (cross), std::move (amp), std::move (lfo), std::move (samples));
    return layout;
}

//==============================================================================
SpectralWeaverProcessor::SpectralWeaverProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "SpectralWeaver", createLayout())
{
    for (auto* id : allParamIds)
    {
        params[id] = parameters.getRawParameterValue (id);
        jassert (params[id] != nullptr);
    }

    formatManager.registerBasicFormats();

    for (int i = 0; i < numVoices; ++i)
    {
        auto* v = new sw::SpectralVoice();
        v->setParams (&renderParams);
        voices[(size_t) i] = v;
        synth.addVoice (v);
    }

    synth.addSound (new sw::SpectralSound());
    synth.setNoteStealingEnabled (true);

    status.fill ("Drop a sample here");
    startTimer (1000);
}

SpectralWeaverProcessor::~SpectralWeaverProcessor()
{
    stopTimer();
    analysisPool.removeAllJobs (true, 10000);
}

bool SpectralWeaverProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SpectralWeaverProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);

    for (auto* v : voices)
        v->prepare (sampleRate);

    keyboardState.reset();
}

//==============================================================================
float SpectralWeaverProcessor::nextLfoValue (int numSamples)
{
    const auto shape = (int) param ("lfoShape");
    const double phase = lfoPhase;
    float value = 0.0f;

    switch (shape)
    {
        case 0:  value = (float) std::sin (juce::MathConstants<double>::twoPi * phase); break;
        case 1:  value = (float) (1.0 - 4.0 * std::abs (phase - 0.5)); break;
        case 2:  value = (float) (2.0 * phase - 1.0); break;
        case 3:  value = phase < 0.5 ? 1.0f : -1.0f; break;
        default: value = lfoRandomValue; break;
    }

    lfoPhase += param ("lfoRate") * numSamples / currentSampleRate;

    if (lfoPhase >= 1.0)
    {
        lfoPhase -= std::floor (lfoPhase);
        lfoRandomValue = lfoRandom.nextFloat() * 2.0f - 1.0f;
    }

    return value;
}

void SpectralWeaverProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();

        if (msg.isControllerOfType (1))
            modWheel = (float) msg.getControllerValue() / 127.0f;
    }

    {
        const juce::SpinLock::ScopedTryLockType lock (imageLock);

        if (lock.isLocked())
            audioImages = images;
    }

    auto& rp = renderParams;
    rp.imageA = audioImages[slotA].get();
    rp.imageB = audioImages[slotB].get();
    rp.levelA = (rp.imageA != nullptr && param ("normA") > 0.5f) ? 1.0f / rp.imageA->samplePeak : 1.0f;
    rp.levelB = (rp.imageB != nullptr && param ("normB") > 0.5f) ? 1.0f / rp.imageB->samplePeak : 1.0f;

    rp.start          = param ("start");
    rp.length         = param ("length");
    rp.speed          = param ("speed");
    rp.freeze         = param ("freeze") > 0.5f;
    rp.loop           = param ("loop") > 0.5f;
    rp.keyTrack       = param ("keyTrack") > 0.5f;
    rp.rootNote       = param ("root");
    rp.fineSemitones  = param ("tune");
    rp.pitchBendRange = param ("bendRange");
    rp.blur           = param ("blur");
    rp.smear          = param ("smear");
    rp.tonal          = param ("tonal");
    rp.tiltDb         = param ("tilt");
    rp.gateDb         = param ("gate");
    rp.warp           = param ("warp");
    rp.shiftHz        = param ("shift");
    rp.phaseRandom    = param ("phaseRand");
    rp.crossMode      = (sw::CrossMode) juce::jlimit (0, (int) sw::CrossMode::numModes - 1, (int) param ("crossMode"));
    rp.crossAmount    = juce::jlimit (0.0f, 1.0f, param ("crossAmount") + modWheel);
    rp.envelopeWidth  = (int) param ("envWidth");
    rp.syncB          = param ("syncB") > 0.5f;
    rp.speedB         = param ("speedB");
    rp.gain           = juce::Decibels::decibelsToGain (param ("gain"));
    rp.extraSemitones = 0.0f;

    // LFO
    const float lfo = nextLfoValue (numSamples) * param ("lfoDepth");

    if (std::abs (lfo) > 0.0f)
    {
        switch ((int) param ("lfoTarget"))
        {
            case 0: rp.start          = juce::jlimit (0.0f, 1.0f, rp.start + lfo * 0.5f); break;
            case 1: rp.extraSemitones = lfo * 12.0f; break;
            case 2: rp.warp           = juce::jlimit (0.25f, 4.0f, rp.warp * std::pow (2.0f, lfo)); break;
            case 3: rp.shiftHz       += lfo * 1000.0f; break;
            case 4: rp.tiltDb        += lfo * 12.0f; break;
            case 5: rp.crossAmount    = juce::jlimit (0.0f, 1.0f, rp.crossAmount + lfo); break;
            case 6: rp.smear          = juce::jlimit (0.0f, 1.0f, rp.smear + lfo); break;
            case 7: rp.blur           = juce::jlimit (0.0f, 1.0f, rp.blur + lfo); break;
            default: break;
        }
    }

    const juce::ADSR::Parameters env { param ("attack"), param ("decay"), param ("sustain"), param ("release") };

    for (auto* v : voices)
        v->setEnvelope (env);

    synth.renderNextBlock (buffer, midi, 0, numSamples);

    // Gentle safety saturation above 0 dBFS
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = d[i];
            const float ax = std::abs (x);

            if (ax > 0.9f)
                d[i] = std::copysign (0.9f + 0.1f * std::tanh ((ax - 0.9f) * 10.0f), x);
        }
    }
}

//==============================================================================
void SpectralWeaverProcessor::loadSample (int slot, const juce::File& file)
{
    const int gen = ++generation[(size_t) slot];
    loading[(size_t) slot] = true;
    setStatus (slot, "Analysing " + file.getFileName() + "...");
    sendChangeMessage();

    analysisPool.addJob ([this, slot, file, gen]
    {
        juce::String error;
        auto image = sw::analyseFile (file, formatManager, error);

        if (generation[(size_t) slot].load() != gen)
            return; // superseded by a newer request

        if (image != nullptr)
            installImage (slot, image, {}); // empty status: the editor describes the image itself
        else
            installImage (slot, getImage (slot), error);
    });
}

void SpectralWeaverProcessor::clearSample (int slot)
{
    ++generation[(size_t) slot];
    installImage (slot, nullptr, "Drop a sample here");
}

void SpectralWeaverProcessor::swapSamples()
{
    // Cancel half-finished loads: they would land in the wrong slot
    ++generation[slotA];
    ++generation[slotB];

    {
        const juce::SpinLock::ScopedLockType lock (imageLock);
        std::swap (images[slotA], images[slotB]);
    }

    {
        const juce::ScopedLock sl (statusLock);
        std::swap (status[slotA], status[slotB]);

        for (auto& s : status)
            if (s.startsWith ("Analysing"))
                s = "Load cancelled by swap";
    }

    loading[slotA] = false;
    loading[slotB] = false;

    // Normalize belongs to the sample, so it travels with it
    auto* normA = parameters.getParameter ("normA");
    auto* normB = parameters.getParameter ("normB");
    const float a = normA->getValue(), b = normB->getValue();

    for (auto [param, value] : { std::pair { normA, b }, std::pair { normB, a } })
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (value);
        param->endChangeGesture();
    }

    sendChangeMessage();
}

void SpectralWeaverProcessor::resetGroup (const juce::String& groupId)
{
    for (const auto* group : getParameterTree().getSubgroups (false))
    {
        if (group->getID() != groupId)
            continue;

        for (auto* param : group->getParameters (true))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getDefaultValue());
            param->endChangeGesture();
        }
    }
}

void SpectralWeaverProcessor::resetAll()
{
    for (auto* id : resettableGroups)
        resetGroup (id);
}

void SpectralWeaverProcessor::installImage (int slot, sw::SpectralImage::Ptr image, const juce::String& newStatus)
{
    if (image != nullptr)
    {
        const juce::ScopedLock gl (graveyardLock);
        graveyard.addIfNotAlreadyThere (image.get());
    }

    {
        const juce::SpinLock::ScopedLockType lock (imageLock);
        images[(size_t) slot] = image;
    }

    loading[(size_t) slot] = false;
    setStatus (slot, newStatus);
    sendChangeMessage();
}

void SpectralWeaverProcessor::timerCallback()
{
    // Free images that only the graveyard still references (never on the audio thread)
    const juce::ScopedLock gl (graveyardLock);

    for (int i = graveyard.size(); --i >= 0;)
        if (graveyard.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            graveyard.remove (i);
}

sw::SpectralImage::Ptr SpectralWeaverProcessor::getImage (int slot) const
{
    const juce::SpinLock::ScopedLockType lock (imageLock);
    return images[(size_t) slot];
}

void SpectralWeaverProcessor::setStatus (int slot, const juce::String& s)
{
    const juce::ScopedLock sl (statusLock);
    status[(size_t) slot] = s;
}

juce::String SpectralWeaverProcessor::getStatus (int slot) const
{
    const juce::ScopedLock sl (statusLock);
    return status[(size_t) slot];
}

float SpectralWeaverProcessor::getVoicePosition (int voice, int slot) const noexcept
{
    const auto* v = voices[(size_t) voice];
    return slot == slotA ? v->getDisplayPositionA() : v->getDisplayPositionB();
}

//==============================================================================
void SpectralWeaverProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    for (int slot = 0; slot < numSlots; ++slot)
    {
        const auto image = getImage (slot);
        state.setProperty (slot == slotA ? "fileA" : "fileB", image != nullptr ? image->file.getFullPathName() : juce::String(), nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectralWeaverProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    const auto state = juce::ValueTree::fromXml (*xml);
    parameters.replaceState (state);

    for (int slot = 0; slot < numSlots; ++slot)
    {
        const juce::String path = state.getProperty (slot == slotA ? "fileA" : "fileB").toString();

        if (path.isEmpty())
        {
            clearSample (slot);
            continue;
        }

        const juce::File file (path);

        if (file.existsAsFile())
            loadSample (slot, file);
        else
            installImage (slot, nullptr, "Missing file: " + file.getFileName());
    }
}

//==============================================================================
juce::AudioProcessorEditor* SpectralWeaverProcessor::createEditor()
{
    return new SpectralWeaverEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralWeaverProcessor();
}

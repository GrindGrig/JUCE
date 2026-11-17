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

    const char* const resettableGroups[] = { "playback", "cross", "spectral", "amp", "lfo1", "lfo2" };
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

    std::array<std::unique_ptr<juce::AudioProcessorParameterGroup>, sw::numLfos> lfoGroups;

    for (int i = 0; i < sw::numLfos; ++i)
    {
        const auto n = juce::String (i + 1);
        const auto id = [i] (const char* suffix) { return juce::ParameterID { sw::lfoParamId (i, suffix), 1 }; };
        const auto fp = [&] (const char* suffix, const juce::String& name, juce::NormalisableRange<float> r, float def, const char* unit, int dec)
        {
            auto attrs = juce::AudioParameterFloatAttributes().withLabel (unit)
                             .withStringFromValueFunction ([dec] (float v, int) { return juce::String (v, dec); });
            return std::make_unique<juce::AudioParameterFloat> (id (suffix), name, r, def, attrs);
        };

        auto g = std::make_unique<juce::AudioProcessorParameterGroup> ("lfo" + n, "LFO " + n, "|");
        g->addChild (std::make_unique<juce::AudioParameterChoice> (id ("Shape"), "LFO " + n + " Shape", sw::lfoShapeNames(), 0),
                     fp ("Rate", "LFO " + n + " Rate", skewed (0.01f, 20.0f, 1.0f), i == 0 ? 0.5f : 2.0f, "Hz", 2),
                     std::make_unique<juce::AudioParameterBool> (id ("Sync"), "LFO " + n + " Tempo Sync", false),
                     std::make_unique<juce::AudioParameterChoice> (id ("Div"), "LFO " + n + " Division", sw::lfoDivisionNames(), 5),
                     fp ("Phase", "LFO " + n + " Phase", { 0.0f, 360.0f }, 0.0f, "deg", 0),
                     fp ("Smooth", "LFO " + n + " Smooth", { 0.0f, 1.0f }, 0.0f, "", 2),
                     fp ("Fade", "LFO " + n + " Fade In", skewed (0.0f, 5.0f, 1.0f), 0.0f, "s", 2),
                     std::make_unique<juce::AudioParameterChoice> (id ("Retrig"), "LFO " + n + " Trigger", sw::lfoTriggerNames(), 0),
                     std::make_unique<juce::AudioParameterBool> (id ("Uni"), "LFO " + n + " Unipolar", false));
        lfoGroups[(size_t) i] = std::move (g);
    }

    // One automatable depth per LFO and target; 0 means "not routed"
    auto modulation = std::make_unique<juce::AudioProcessorParameterGroup> ("modulation", "Modulation Depths", "|");

    for (int i = 0; i < sw::numLfos; ++i)
        for (int t = 0; t < sw::modTargetIds().size(); ++t)
            modulation->addChild (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { sw::modDepthId (i, sw::modTargetIds()[t]), 1 },
                "LFO " + juce::String (i + 1) + " > " + sw::modTargetNames()[t],
                juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })));

    auto samples = std::make_unique<juce::AudioProcessorParameterGroup> ("samples", "Samples", "|");
    samples->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "normA", 1 }, "Normalize A", false),
                       std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "normB", 1 }, "Normalize B", false));

    layout.add (std::move (playback), std::move (spectral), std::move (cross), std::move (amp), std::move (lfoGroups[0]), std::move (lfoGroups[1]),
                std::move (modulation), std::move (samples));
    return layout;
}

//==============================================================================
SpectralWeaverProcessor::SpectralWeaverProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "SpectralWeaver", createLayout())
{
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            params[ranged->paramID.toStdString()] = parameters.getRawParameterValue (ranged->paramID);

    for (int i = 0; i < sw::numLfos; ++i)
    {
        const auto raw = [&] (const char* suffix) { return parameters.getRawParameterValue (sw::lfoParamId (i, suffix)); };
        lfoParams[(size_t) i] = { raw ("Shape"), raw ("Rate"), raw ("Sync"), raw ("Div"), raw ("Phase"),
                                  raw ("Smooth"), raw ("Fade"), raw ("Retrig"), raw ("Uni") };
    }

    jassert (sw::modTargetIds().size() <= (int) modValues.size());

    for (const auto& id : sw::modTargetIds())
    {
        ModTarget t;
        t.base  = parameters.getRawParameterValue (id);
        t.range = parameters.getParameterRange (id);
        t.isInt = id == "envWidth";

        for (int i = 0; i < sw::numLfos; ++i)
            t.depth[(size_t) i] = parameters.getRawParameterValue (sw::modDepthId (i, id));

        modTargets.push_back (t);
    }

    for (auto& m : modulatedNorm)
        m = -1.0f;

    loadCustomShapesFromState();

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

    // Host tempo and position, for tempo-synced LFOs
    double bpm = 120.0;
    std::optional<double> ppq;

    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto pos = hostPlayHead->getPosition())
        {
            if (const auto b = pos->getBpm())
                bpm = juce::jlimit (20.0, 999.0, *b);

            if (pos->getIsPlaying())
                if (const auto q = pos->getPpqPosition())
                    ppq = *q;
        }
    }

    hostBpm = bpm;

    // Modulation is refreshed every 64 samples
    constexpr int chunk = 64;

    for (int start = 0; start < numSamples; start += chunk)
        renderChunk (buffer, midi, start, juce::jmin (chunk, numSamples - start), ppq, bpm);

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

void SpectralWeaverProcessor::renderChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int start, int length,
                                           std::optional<double> ppqAtBlockStart, double bpm)
{
    const double sr = currentSampleRate;

    // Note-ons in this chunk restart the fade-in and retrigger LFOs set to do so
    for (const auto meta : midi)
    {
        if (meta.samplePosition >= start && meta.samplePosition < start + length && meta.getMessage().isNoteOn())
        {
            secondsSinceNote = 0.0;

            for (int i = 0; i < sw::numLfos; ++i)
                if (lfoParams[(size_t) i].retrig->load() > 0.5f)
                    lfos[(size_t) i].retrigger();

            break;
        }
    }

    std::array<float, sw::numLfos> lfoOut {};
    const auto& divisions = sw::lfoDivisions();

    for (int i = 0; i < sw::numLfos; ++i)
    {
        const auto& lp = lfoParams[(size_t) i];
        const bool sync = lp.sync->load() > 0.5f;
        const double beats = divisions[(size_t) juce::jlimit (0, (int) divisions.size() - 1, (int) lp.div->load())].beats;

        sw::Lfo::Settings s;
        s.shape       = juce::jlimit (0, sw::numLfoShapes - 1, (int) lp.shape->load());
        s.rateHz      = sync ? bpm / 60.0 / beats : (double) lp.rate->load();
        s.phaseOffset = lp.phase->load() / 360.0f;
        s.smooth      = lp.smooth->load();
        s.fadeSeconds = lp.fade->load();
        s.unipolar    = lp.uni->load() > 0.5f;

        std::optional<double> cycle;

        if (sync && ppqAtBlockStart.has_value())
            cycle = (*ppqAtBlockStart + start / sr * bpm / 60.0) / beats;

        lfoOut[(size_t) i] = lfos[(size_t) i].process (s, length, sr, customTables[(size_t) i], secondsSinceNote, cycle);
    }

    secondsSinceNote += length / sr;

    // Every target: normalised base position + sum of (depth * LFO), clamped to the knob's travel
    for (size_t t = 0; t < modTargets.size(); ++t)
    {
        const auto& mt = modTargets[t];
        const float base = mt.base->load();
        float offset = 0.0f;
        bool routed = false;

        for (size_t i = 0; i < (size_t) sw::numLfos; ++i)
        {
            const float d = mt.depth[i]->load();
            routed = routed || ! juce::exactlyEqual (d, 0.0f);
            offset += d * lfoOut[i];
        }

        if (! routed)
        {
            modValues[t] = base;
            modulatedNorm[t] = -1.0f;
            continue;
        }

        const float norm = juce::jlimit (0.0f, 1.0f, mt.range.convertTo0to1 (base) + offset);
        float value = mt.range.convertFrom0to1 (norm);

        if (mt.isInt)
            value = (float) juce::roundToInt (value);

        modValues[t] = value;
        modulatedNorm[t] = norm;
    }

    // Indices follow sw::modTargetIds()
    const auto mv = [this] (int index) { return modValues[(size_t) index]; };
    auto& rp = renderParams;

    rp.start          = mv (0);
    rp.length         = mv (1);
    rp.speed          = mv (2);
    rp.fineSemitones  = mv (3);
    rp.crossAmount    = juce::jlimit (0.0f, 1.0f, mv (4) + modWheel);
    rp.envelopeWidth  = (int) mv (5);
    rp.speedB         = mv (6);
    rp.blur           = mv (7);
    rp.smear          = mv (8);
    rp.tonal          = mv (9);
    rp.tiltDb         = mv (10);
    rp.gateDb         = mv (11);
    rp.warp           = mv (12);
    rp.shiftHz        = mv (13);
    rp.phaseRandom    = mv (14);
    rp.gain           = juce::Decibels::decibelsToGain (mv (16));

    rp.freeze         = param ("freeze") > 0.5f;
    rp.loop           = param ("loop") > 0.5f;
    rp.keyTrack       = param ("keyTrack") > 0.5f;
    rp.rootNote       = param ("root");
    rp.pitchBendRange = param ("bendRange");
    rp.crossMode      = (sw::CrossMode) juce::jlimit (0, (int) sw::CrossMode::numModes - 1, (int) param ("crossMode"));
    rp.syncB          = param ("syncB") > 0.5f;
    rp.extraSemitones = 0.0f;

    const juce::ADSR::Parameters env { param ("attack"), param ("decay"), mv (15), param ("release") };

    for (auto* v : voices)
        v->setEnvelope (env);

    synth.renderNextBlock (buffer, midi, start, length);
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

    for (int i = 0; i < sw::numLfos; ++i)
    {
        if (groupId != "lfo" + juce::String (i + 1))
            continue;

        for (const auto& target : getRoutes (i))
            removeRoute (i, target);

        setCustomPoints (i, sw::defaultCustomPoints());
    }
}

//==============================================================================
void SpectralWeaverProcessor::setParamValue (const juce::String& id, float normalisedValue)
{
    if (auto* p = parameters.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (normalisedValue);
        p->endChangeGesture();
    }
}

juce::ValueTree SpectralWeaverProcessor::routesTree()
{
    return parameters.state.getOrCreateChildWithName ("ROUTES", nullptr);
}

void SpectralWeaverProcessor::addRoute (int lfo, const juce::String& targetId)
{
    if (! sw::modTargetIds().contains (targetId) || lfo < 0 || lfo >= sw::numLfos)
        return;

    if (! hasRoute (lfo, targetId))
    {
        juce::ValueTree route ("ROUTE");
        route.setProperty ("lfo", lfo, nullptr);
        route.setProperty ("target", targetId, nullptr);
        routesTree().appendChild (route, nullptr);
    }

    const auto depthId = sw::modDepthId (lfo, targetId);

    if (juce::exactlyEqual (parameters.getRawParameterValue (depthId)->load(), 0.0f))
        setParamValue (depthId, parameters.getParameterRange (depthId).convertTo0to1 (0.5f));
}

void SpectralWeaverProcessor::removeRoute (int lfo, const juce::String& targetId)
{
    auto routes = routesTree();

    for (int i = routes.getNumChildren(); --i >= 0;)
    {
        const auto r = routes.getChild (i);

        if ((int) r["lfo"] == lfo && r["target"].toString() == targetId)
            routes.removeChild (i, nullptr);
    }

    const auto depthId = sw::modDepthId (lfo, targetId);

    if (auto* p = parameters.getParameter (depthId))
        setParamValue (depthId, p->getDefaultValue());
}

bool SpectralWeaverProcessor::hasRoute (int lfo, const juce::String& targetId) const
{
    const auto routes = parameters.state.getChildWithName ("ROUTES");

    for (const auto& r : routes)
        if ((int) r["lfo"] == lfo && r["target"].toString() == targetId)
            return true;

    return false;
}

juce::StringArray SpectralWeaverProcessor::getRoutes (int lfo) const
{
    juce::StringArray result;

    for (const auto& r : parameters.state.getChildWithName ("ROUTES"))
        if ((int) r["lfo"] == lfo)
            result.addIfNotAlreadyThere (r["target"].toString());

    return result;
}

sw::ShapePoints SpectralWeaverProcessor::getCustomPoints (int lfo) const
{
    return customPoints[(size_t) lfo];
}

void SpectralWeaverProcessor::setCustomPoints (int lfo, sw::ShapePoints points)
{
    for (auto& p : points)
        p = { juce::jlimit (0.0f, 0.999f, p.x), juce::jlimit (-1.0f, 1.0f, p.y) };

    std::sort (points.begin(), points.end(), [] (auto a, auto b) { return a.x < b.x; });

    if (points.size() < 2)
        points = sw::defaultCustomPoints();

    customPoints[(size_t) lfo] = points;
    customTables[(size_t) lfo].setPoints (points);

    auto shapes = parameters.state.getOrCreateChildWithName ("LFOSHAPES", nullptr);
    auto shape = shapes.getChildWithProperty ("index", lfo);

    if (! shape.isValid())
    {
        shape = juce::ValueTree ("SHAPE");
        shape.setProperty ("index", lfo, nullptr);
        shapes.appendChild (shape, nullptr);
    }

    shape.setProperty ("points", sw::pointsToString (points), nullptr);
}

void SpectralWeaverProcessor::loadCustomShapesFromState()
{
    const auto shapes = parameters.state.getChildWithName ("LFOSHAPES");

    for (int i = 0; i < sw::numLfos; ++i)
    {
        const auto shape = shapes.getChildWithProperty ("index", i);
        setCustomPoints (i, shape.isValid() ? sw::pointsFromString (shape["points"].toString()) : sw::defaultCustomPoints());
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
    loadCustomShapesFromState();

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

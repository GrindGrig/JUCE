#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using Attr = juce::AudioParameterFloatAttributes;

    std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name, float lo, float hi, float step,
                                                           float def, std::function<juce::String (float)> text)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, step), def,
                                                            Attr().withStringFromValueFunction ([text] (float v, int) { return text (v); }));
    }

    juce::String percent (float v)    { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
    juce::String times (float v)      { return juce::String::charToString (0x00d7) + juce::String (v, 2); }
}

juce::AudioProcessorValueTreeState::ParameterLayout TimbalProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::mode, 1 }, "Method", modeNames(), 0));
    layout.add (floatParam (ids::transpose, "Transpose", -24.0f, 24.0f, 1.0f, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; }));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::autoOctave, 1 }, "Match B's octave", true));
    layout.add (floatParam (ids::snap, "Pitch snap", 0.0f, 1.0f, 0.01f, 0.3f, percent));
    layout.add (floatParam (ids::brightness, "Loudness to brightness", 0.0f, 1.5f, 0.01f, 1.0f, times));
    layout.add (floatParam (ids::noise, "Noise and breath", 0.0f, 2.5f, 0.01f, 1.0f, times));
    layout.add (floatParam (ids::consonants, "Consonant colour", 0.0f, 1.0f, 0.01f, 0.4f, percent));
    layout.add (floatParam (ids::vibrato, "Vibrato", 0.0f, 50.0f, 1.0f, 0.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " ct"; }));
    layout.add (floatParam (ids::glide, "Glide", 0.0f, 200.0f, 1.0f, 25.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::grain, 1 }, "Grain length", juce::StringArray { "Short", "Long" }, 1));
    layout.add (floatParam (ids::continuity, "Continuity", 0.0f, 1.0f, 0.01f, 0.5f, percent));
    layout.add (floatParam (ids::pitchFix, "Pitch correction", 0.0f, 1.0f, 0.01f, 1.0f, percent));
    layout.add (floatParam (ids::detail, "Envelope detail", 12.0f, 64.0f, 2.0f, 32.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " bands"; }));
    layout.add (floatParam (ids::flatten, "Flatten B's colour", 0.0f, 1.0f, 0.01f, 0.8f, percent));
    layout.add (floatParam (ids::air, "Air", 0.0f, 0.6f, 0.01f, 0.1f, percent));
    layout.add (floatParam (ids::gate, "Gate", -80.0f, -20.0f, 0.5f, -55.0f, [] (float v) { return juce::String (v, 1) + " dB"; }));
    layout.add (floatParam (ids::mix, "Mix", 0.0f, 1.0f, 0.01f, 1.0f, percent));
    layout.add (floatParam (ids::gain, "Output gain", -24.0f, 12.0f, 0.1f, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::swap, 1 }, "Swap A and B", false,
                                                            juce::AudioParameterBoolAttributes().withAutomatable (false)));
    return layout;
}

//==============================================================================
TimbalProcessor::TimbalProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "TiMBAL", createLayout())
{
    const auto get = [this] (const juce::String& id) { return parameters.getRawParameterValue (id); };
    raw = { get (ids::mode), get (ids::transpose), get (ids::autoOctave), get (ids::snap), get (ids::brightness), get (ids::noise),
            get (ids::consonants), get (ids::vibrato), get (ids::glide), get (ids::grain), get (ids::continuity), get (ids::pitchFix),
            get (ids::detail), get (ids::flatten), get (ids::air), get (ids::gate), get (ids::mix), get (ids::gain), get (ids::swap) };

    setLatencySamples (engine.latencySamples());
    startTimer (250);
}

TimbalProcessor::~TimbalProcessor()
{
    stopTimer();
    pool.removeAllJobs (true, 10000);
}

bool TimbalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void TimbalProcessor::runOnMessageThread (std::function<void (TimbalProcessor&)> fn)
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm != nullptr && mm->isThisTheMessageThread()) { fn (*this); return; }
    juce::MessageManager::callAsync ([weak = juce::WeakReference<TimbalProcessor> (this), fn] { if (weak != nullptr) fn (*weak); });
}

void TimbalProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rate = sampleRate;
    engine.prepare (sampleRate);
    setLatencySamples (engine.latencySamples());

    const auto block = (size_t) juce::jmax (512, samplesPerBlock);
    monoIn.assign (block, 0.0f);
    performanceIn.assign (block, 0.0f);
    wet.assign (block, 0.0f);
    dry.assign (block, 0.0f);

    captureRing.assign ((size_t) juce::nextPowerOfTwo ((int) (sampleRate * liveWindowSeconds * 1.5)), 0.0f);
    captureWritten = 0;
    captureLoudSamples = 0;
    freeRunPosition = 0;

    gainSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.reset (sampleRate, 0.03);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw.gain->load()));
    mixSmoothed.setCurrentAndTargetValue (raw.mix->load());

    // the clips are analysed at the session's rate (the mosaic and vocoder need that)
    for (auto slot : { Slot::timbreClip, Slot::performanceClip })
    {
        const auto* model = getModel (slot);
        if ((model == nullptr || ! juce::exactlyEqual (model->sampleRate, sampleRate)) && ! isLoading (slot))
            runOnMessageThread ([slot] (TimbalProcessor& p) { p.requestClip (slot, p.getSource (slot)); });
    }
}

tt::Settings TimbalProcessor::readSettings() const noexcept
{
    tt::Settings s;
    s.mode = (tt::Mode) juce::jlimit (0, 2, (int) raw.mode->load());
    s.transpose = raw.transpose->load();
    s.autoOctave = raw.autoOctave->load() > 0.5f;
    s.snap = raw.snap->load();
    s.brightness = raw.brightness->load();
    s.noise = raw.noise->load();
    s.consonants = raw.consonants->load();
    s.vibratoCents = raw.vibrato->load();
    s.glideMs = raw.glide->load();
    s.grain = (int) raw.grain->load();
    s.continuity = raw.continuity->load();
    s.pitchFix = raw.pitchFix->load();
    s.bands = juce::jlimit (12, 64, (int) raw.detail->load());
    s.flatten = raw.flatten->load();
    s.air = raw.air->load();
    s.gateDb = raw.gate->load();
    return s;
}

void TimbalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels(), total = buffer.getNumSamples();
    const auto settings = readSettings();
    const bool swapped = isSwapped();
    const auto* timbre = getModel (swapped ? Slot::liveTimbre : Slot::timbreClip);
    const auto* clip = swapped ? getModel (Slot::performanceClip) : nullptr;
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (raw.gain->load()));
    mixSmoothed.setTargetValue (raw.mix->load());

    // the performance clip follows the host's timeline while it plays; with no timeline it runs freely
    bool hasTimeline = false, playing = false;
    juce::int64 hostSample = 0;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
            if (auto t = pos->getTimeInSamples())
            {
                hasTimeline = true;
                playing = pos->getIsPlaying();
                hostSample = *t;
            }

    const float gateLinear = juce::Decibels::decibelsToGain (settings.gateDb);
    const auto captureMask = (juce::int64) captureRing.size() - 1;

    for (int start = 0; start < total;)
    {
        const int n = juce::jmin ((int) monoIn.size(), total - start);

        double energy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < numIn; ++c) s += buffer.getReadPointer (c)[start + i];
            monoIn[(size_t) i] = numIn > 0 ? s / (float) numIn : 0.0f;
            energy += (double) monoIn[(size_t) i] * monoIn[(size_t) i];
        }

        if (swapped)
        {
            // keep the recent input for the live timbre
            auto w = captureWritten.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) captureRing[(size_t) ((w + i) & captureMask)] = monoIn[(size_t) i];
            captureWritten.store (w + n, std::memory_order_release);
            if (std::sqrt (energy / juce::jmax (1, n)) > gateLinear) captureLoudSamples += n;

            // play the performance clip (clips are normalised to full scale; play them back nearer a typical track level)
            constexpr float clipLevel = 0.5f;
            const bool run = clip != nullptr && ! clip->audio.empty() && (playing || ! hasTimeline);
            const auto len = clip != nullptr ? (juce::int64) clip->audio.size() : (juce::int64) 1;
            for (int i = 0; i < n; ++i)
            {
                if (! run) { performanceIn[(size_t) i] = 0.0f; continue; }
                // read ahead by the latency so the result lines up with the timeline after delay compensation
                const auto at = hasTimeline ? hostSample + start + i + engine.latencySamples() : freeRunPosition++;
                performanceIn[(size_t) i] = clipLevel * clip->audio[(size_t) (((at % len) + len) % len)];
            }
            if (run)
            {
                const auto at = hasTimeline ? hostSample + start + n : freeRunPosition;
                clipPosition.store ((float) (((at % len) + len) % len) / (float) len);
            }
            else clipPosition.store (-1.0f);
        }
        else clipPosition.store (-1.0f);

        engine.process (swapped ? performanceIn.data() : monoIn.data(), monoIn.data(), wet.data(), dry.data(), n, settings, timbre, swapped);

        for (int i = 0; i < n; ++i)
        {
            const float m = mixSmoothed.getNextValue(), g = gainSmoothed.getNextValue();
            const float y = (dry[(size_t) i] * (1.0f - m) + wet[(size_t) i] * m) * g;
            for (int c = 0; c < numOut; ++c) buffer.getWritePointer (c)[start + i] = y;
        }

        start += n;
    }
}

//==============================================================================
std::vector<float> TimbalProcessor::decodeFile (const juce::File& file, double targetRate, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Could not read " + file.getFileName() + ". Try a WAV, AIFF, FLAC, OGG or MP3 file.";
        return {};
    }

    const auto fileRate = reader->sampleRate;
    const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (fileRate * tt::maxTimbreSeconds));
    if (length < (int) (fileRate * 0.2))
    {
        error = file.getFileName() + " is too short. Use at least a second of audio.";
        return {};
    }

    juce::AudioBuffer<float> buf ((int) reader->numChannels, length);
    reader->read (&buf, 0, length, 0, true, true);

    std::vector<float> mono ((size_t) length, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < length; ++i) mono[(size_t) i] += buf.getReadPointer (c)[i] / (float) buf.getNumChannels();

    std::vector<float> out;
    if (juce::exactlyEqual (fileRate, targetRate)) out = std::move (mono);
    else
    {
        const double ratio = fileRate / targetRate;
        out.assign ((size_t) ((double) length / ratio), 0.0f);
        juce::LagrangeInterpolator interp;
        interp.process (ratio, mono.data(), out.data(), (int) out.size(), (int) mono.size(), 0);
    }

    float peak = 0.0f;
    for (auto v : out) peak = juce::jmax (peak, std::abs (v));
    if (peak <= 1.0e-6f)
    {
        error = file.getFileName() + " is silent.";
        return {};
    }
    for (auto& v : out) v *= 0.9f / peak;
    return out;
}

std::unique_ptr<tt::TimbreModel> TimbalProcessor::buildModel (Slot slot, const ClipSource& src, double sampleRate, juce::String& error)
{
    if (src.builtIn >= 0)
    {
        const auto names = exampleNames (slot);
        const int index = juce::jlimit (0, names.size() - 1, src.builtIn);
        auto audio = slot == Slot::performanceClip ? tt::synthesisePerformance (index, sampleRate) : tt::synthesiseBuiltIn (index, sampleRate);
        return tt::analyseTimbre (std::move (audio), sampleRate, names[index]);
    }

    auto audio = decodeFile (src.file, sampleRate, error);
    if (audio.empty()) return nullptr;
    auto model = tt::analyseTimbre (std::move (audio), sampleRate, src.file.getFileName());
    if (model->active.empty()) { error = src.file.getFileName() + " is too quiet to use."; return nullptr; }
    return model;
}

void TimbalProcessor::requestClip (Slot slot, const ClipSource& src)
{
    auto& s = slots[(size_t) slot];
    s.source = src;
    const int gen = ++s.generation;
    s.loading = true;
    s.error.clear();
    sendChangeMessage();

    const double sampleRate = rate;
    pool.addJob ([slot, src, gen, sampleRate, weak = juce::WeakReference<TimbalProcessor> (this)]
    {
        juce::String error;
        std::shared_ptr<tt::TimbreModel> shared (buildModel (slot, src, sampleRate, error).release());

        juce::MessageManager::callAsync ([weak, slot, gen, shared, error]
        {
            auto* self = weak.get();
            if (self == nullptr) return;
            auto& st = self->slots[(size_t) slot];
            if (gen != st.generation.load()) return;

            st.loading = false;
            if (shared != nullptr)
            {
                const bool staleRate = ! juce::exactlyEqual (shared->sampleRate, self->rate);
                self->install (slot, std::make_unique<tt::TimbreModel> (std::move (*shared)));
                if (staleRate) { self->requestClip (slot, st.source); return; }   // the session's rate changed meanwhile
            }
            else st.error = error;
            self->sendChangeMessage();
        });
    });
}

bool TimbalProcessor::loadClipNow (Slot slot, const ClipSource& src, double sampleRate)
{
    auto& s = slots[(size_t) slot];
    s.source = src;
    juce::String error;
    auto model = buildModel (slot, src, sampleRate, error);
    s.error = error;
    if (model == nullptr) return false;
    install (slot, std::move (model));
    return true;
}

//==============================================================================
std::vector<float> TimbalProcessor::copyCapturedInput() const
{
    const auto written = captureWritten.load (std::memory_order_acquire);
    const auto size = (juce::int64) captureRing.size();
    // stay clear of the part the audio thread may be writing
    const auto count = juce::jmin (written, (juce::int64) (rate * liveWindowSeconds), size - 8192);
    std::vector<float> out ((size_t) juce::jmax ((juce::int64) 0, count));
    for (juce::int64 k = 0; k < count; ++k)
        out[(size_t) k] = captureRing[(size_t) ((written - count + k) & (size - 1))];
    return out;
}

namespace
{
    std::unique_ptr<tt::TimbreModel> learnLive (std::vector<float> audio, double sampleRate)
    {
        if (audio.size() < (size_t) (sampleRate * 0.5)) return nullptr;
        float peak = 0.0f;
        for (auto v : audio) peak = juce::jmax (peak, std::abs (v));
        if (peak < 1.0e-4f) return nullptr;
        for (auto& v : audio) v *= 0.9f / peak;
        auto model = tt::analyseTimbre (std::move (audio), sampleRate, "Live input");
        return model->active.empty() ? nullptr : std::move (model);
    }
}

void TimbalProcessor::startLiveLearn()
{
    liveLearnBusy = true;
    captureLoudSamples = 0;
    lastLiveLearn = juce::Time::getMillisecondCounter();

    pool.addJob ([audio = copyCapturedInput(), sampleRate = rate, weak = juce::WeakReference<TimbalProcessor> (this)]() mutable
    {
        std::shared_ptr<tt::TimbreModel> shared (learnLive (std::move (audio), sampleRate).release());
        juce::MessageManager::callAsync ([weak, shared]
        {
            auto* self = weak.get();
            if (self == nullptr) return;
            self->liveLearnBusy = false;
            if (shared != nullptr)
            {
                self->install (Slot::liveTimbre, std::make_unique<tt::TimbreModel> (std::move (*shared)));
                self->sendChangeMessage();
            }
        });
    });
}

bool TimbalProcessor::learnFromInputNow()
{
    auto model = learnLive (copyCapturedInput(), rate);
    if (model == nullptr) return false;
    install (Slot::liveTimbre, std::move (model));
    lastLiveLearn = juce::Time::getMillisecondCounter();
    return true;
}

double TimbalProcessor::secondsSinceLiveLearn() const
{
    if (getModel (Slot::liveTimbre) == nullptr) return -1.0;
    return (juce::Time::getMillisecondCounter() - lastLiveLearn) / 1000.0;
}

void TimbalProcessor::install (Slot slot, std::unique_ptr<tt::TimbreModel> model)
{
    // the audio thread may still be reading the old model, so it is freed a little later
    auto& s = slots[(size_t) slot];
    if (s.owned != nullptr) retired.emplace_back (std::move (s.owned), juce::Time::getMillisecondCounter());
    s.owned = std::move (model);
    s.current.store (s.owned.get(), std::memory_order_release);
}

void TimbalProcessor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (auto& r) { return now - r.second > 1500; }), retired.end());

    // while the input is B, relearn its timbre about once a second, once there is something to learn from
    if (isSwapped() && ! liveLearnBusy.load() && now - lastLiveLearn >= 1000
        && captureLoudSamples.load() > (juce::int64) (rate * 0.25))
        startLiveLearn();
}

void TimbalProcessor::resetParameters (const juce::StringArray& paramIds)
{
    for (auto& id : paramIds)
        if (auto* p = parameters.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
}

//==============================================================================
void TimbalProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = parameters.copyState();
    const auto& t = slots[(size_t) Slot::timbreClip].source;
    const auto& p = slots[(size_t) Slot::performanceClip].source;
    state.setProperty ("timbreBuiltIn", t.builtIn, nullptr);
    state.setProperty ("timbreFile", t.file.getFullPathName(), nullptr);
    state.setProperty ("performanceBuiltIn", p.builtIn, nullptr);
    state.setProperty ("performanceFile", p.file.getFullPathName(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TimbalProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType())) return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto readSource = [&state] (const char* builtInKey, const char* fileKey)
    {
        ClipSource src;
        src.builtIn = state.getProperty (builtInKey, 0);
        const auto path = state.getProperty (fileKey).toString();
        if (src.builtIn < 0 && juce::File::isAbsolutePath (path)) src.file = juce::File (path);
        else src.builtIn = juce::jmax (0, src.builtIn);
        return src;
    };
    const auto timbreSrc = readSource ("timbreBuiltIn", "timbreFile");
    const auto performanceSrc = readSource ("performanceBuiltIn", "performanceFile");
    parameters.replaceState (state);

    runOnMessageThread ([timbreSrc, performanceSrc] (TimbalProcessor& p)
    {
        if (! (timbreSrc == p.getSource (Slot::timbreClip)) || p.getModel (Slot::timbreClip) == nullptr)
            p.requestClip (Slot::timbreClip, timbreSrc);
        if (! (performanceSrc == p.getSource (Slot::performanceClip)) || p.getModel (Slot::performanceClip) == nullptr)
            p.requestClip (Slot::performanceClip, performanceSrc);
    });
}

juce::AudioProcessorEditor* TimbalProcessor::createEditor() { return new TimbalEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TimbalProcessor(); }

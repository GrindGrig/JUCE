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
            get (ids::detail), get (ids::flatten), get (ids::air), get (ids::gate), get (ids::mix), get (ids::gain) };

    setLatencySamples (engine.latencySamples());
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

void TimbalProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rate = sampleRate;
    engine.prepare (sampleRate);
    setLatencySamples (engine.latencySamples());

    const auto block = (size_t) juce::jmax (512, samplesPerBlock);
    monoIn.assign (block, 0.0f);
    wet.assign (block, 0.0f);
    dry.assign (block, 0.0f);

    gainSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.reset (sampleRate, 0.03);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw.gain->load()));
    mixSmoothed.setCurrentAndTargetValue (raw.mix->load());

    // the mosaic and vocoder need the timbre analysed at the session's rate
    const auto* model = getTimbre();
    if ((model == nullptr || ! juce::exactlyEqual (model->sampleRate, sampleRate)) && ! loading.load())
    {
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
            && juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread())
            requestTimbre (source);
        else
            juce::MessageManager::callAsync ([weak = juce::WeakReference<TimbalProcessor> (this)]
                                             { if (weak != nullptr) weak->requestTimbre (weak->source); });
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
    const auto* model = current.load (std::memory_order_acquire);
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (raw.gain->load()));
    mixSmoothed.setTargetValue (raw.mix->load());

    for (int start = 0; start < total;)
    {
        const int n = juce::jmin ((int) monoIn.size(), total - start);

        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < numIn; ++c) s += buffer.getReadPointer (c)[start + i];
            monoIn[(size_t) i] = numIn > 0 ? s / (float) numIn : 0.0f;
        }

        engine.process (monoIn.data(), wet.data(), dry.data(), n, settings, model);

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

std::unique_ptr<tt::TimbreModel> TimbalProcessor::buildModel (const TimbreSource& src, double sampleRate, juce::String& error) const
{
    if (src.builtIn >= 0)
        return tt::analyseTimbre (tt::synthesiseBuiltIn (src.builtIn, sampleRate), sampleRate, tt::builtInTimbreNames()[src.builtIn]);

    auto audio = decodeFile (src.file, sampleRate, error);
    if (audio.empty()) return nullptr;
    auto model = tt::analyseTimbre (std::move (audio), sampleRate, src.file.getFileName());
    if (model->active.empty()) { error = src.file.getFileName() + " is too quiet to learn from."; return nullptr; }
    return model;
}

void TimbalProcessor::requestTimbre (const TimbreSource& src)
{
    source = src;
    const int gen = ++generation;
    loading = true;
    lastError.clear();
    sendChangeMessage();

    const double sampleRate = rate;
    pool.addJob ([this, src, gen, sampleRate, weak = juce::WeakReference<TimbalProcessor> (this)]
    {
        juce::String error;
        auto model = buildModel (src, sampleRate, error);
        std::shared_ptr<tt::TimbreModel> shared (model.release());

        juce::MessageManager::callAsync ([weak, gen, shared, error]() mutable
        {
            auto* self = weak.get();
            if (self == nullptr || gen != self->generation.load()) return;

            self->loading = false;
            if (shared != nullptr)
            {
                const bool staleRate = ! juce::exactlyEqual (shared->sampleRate, self->rate);
                self->install (std::unique_ptr<tt::TimbreModel> (new tt::TimbreModel (std::move (*shared))));
                if (staleRate) { self->requestTimbre (self->source); return; }   // the session's rate changed meanwhile
            }
            else
                self->lastError = error;
            self->sendChangeMessage();
        });
    });
}

bool TimbalProcessor::loadTimbreNow (const TimbreSource& src, double sampleRate)
{
    source = src;
    juce::String error;
    auto model = buildModel (src, sampleRate, error);
    lastError = error;
    if (model == nullptr) return false;
    install (std::move (model));
    return true;
}

void TimbalProcessor::install (std::unique_ptr<tt::TimbreModel> model)
{
    // the audio thread may still be reading the old model, so it is freed a little later
    if (live != nullptr) retired.emplace_back (std::move (live), juce::Time::getMillisecondCounter());
    live = std::move (model);
    current.store (live.get(), std::memory_order_release);
    if (! retired.empty()) startTimer (500);
}

void TimbalProcessor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (auto& r) { return now - r.second > 1500; }), retired.end());
    if (retired.empty()) stopTimer();
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
    state.setProperty ("timbreBuiltIn", source.builtIn, nullptr);
    state.setProperty ("timbreFile", source.file.getFullPathName(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TimbalProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            TimbreSource src;
            src.builtIn = state.getProperty ("timbreBuiltIn", 0);
            const auto path = state.getProperty ("timbreFile").toString();
            if (src.builtIn < 0 && juce::File::isAbsolutePath (path)) src.file = juce::File (path);
            else src.builtIn = juce::jmax (0, src.builtIn);
            parameters.replaceState (state);

            if (! (src == source) || getTimbre() == nullptr)
                requestTimbre (src);
        }
}

juce::AudioProcessorEditor* TimbalProcessor::createEditor() { return new TimbalEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TimbalProcessor(); }

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MidiExport.h"

namespace gf
{

GrooveForgeProcessor::GrooveForgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int l = 0; l < NumLanes; ++l)
        lanes[(size_t) l].midiNote = laneInfo (l).defaultNote;

    params.genre = Genre::House;
    params.swing = Generator::info (params.genre).swing;
    params.seed = (int) (juce::Time::currentTimeMillis() & 0x7fffffff);
    tempo = Generator::info (params.genre).tempo;
    generate (-1, false);
}

bool GrooveForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void GrooveForgeProcessor::prepareToPlay (double sampleRate, int)
{
    synth.prepare (sampleRate);
    triggers.reserve (4096);
    numPendingOffs = 0;
    lastBlockEndPpq = -1.0;
}

//==============================================================================
void GrooveForgeProcessor::pushToAudioThread()
{
    std::vector<DrumNote> notes = pattern.notes;
    auto laneCopy = lanes;
    const double len = pattern.lengthInBeats();

    const juce::SpinLock::ScopedLockType sl (audioLock);
    std::swap (audioNotes, notes);
    audioLanes = laneCopy;
    audioLength = len;
}

void GrooveForgeProcessor::modifyPattern (const std::function<void (Pattern&)>& fn)
{
    fn (pattern);
    pattern.sortNotes();
    pushToAudioThread();
    sendChangeMessage();
}

void GrooveForgeProcessor::modifyLane (int lane, const std::function<void (LaneSettings&)>& fn)
{
    fn (lanes[(size_t) lane]);
    pushToAudioThread();
    sendChangeMessage();
}

void GrooveForgeProcessor::generate (int onlyLane, bool newSeed)
{
    if (newSeed)
        params.seed = juce::Random::getSystemRandom().nextInt (0x7fffffff);

    std::array<bool, NumLanes> mask {};
    for (int l = 0; l < NumLanes; ++l)
        mask[(size_t) l] = onlyLane >= 0 ? (l == onlyLane) : ! lanes[(size_t) l].locked;

    if (onlyLane >= 0)
    {
        // Regenerating a single layer: keep the pattern's overall seed for the other
        // layers but give this layer a fresh random take.
        auto p = params;
        p.seed = newSeed ? juce::Random::getSystemRandom().nextInt (0x7fffffff) : params.seed;
        Generator::generate (pattern, p, mask);
    }
    else
    {
        Generator::generate (pattern, params, mask);
    }

    pushToAudioThread();
    sendChangeMessage();
}

void GrooveForgeProcessor::setTimeSignature (int num, int den, const juce::String& grouping)
{
    pattern.numerator = num;
    pattern.denominator = den;
    pattern.grouping = Meter::create (num, den, grouping).grouping;

    // Locked layers keep their notes (trimmed); everything else is re-written for the new meter.
    const double len = pattern.lengthInBeats();
    pattern.notes.erase (std::remove_if (pattern.notes.begin(), pattern.notes.end(),
                                         [len] (const DrumNote& n) { return n.beat >= len; }),
                         pattern.notes.end());
    generate (-1, false);
}

void GrooveForgeProcessor::setBars (int newBars)
{
    newBars = juce::jlimit (1, 16, newBars);
    modifyPattern ([newBars] (Pattern& p)
    {
        const double oldLen = p.lengthInBeats();
        const int oldBars = p.bars;
        p.bars = newBars;
        const double newLen = p.lengthInBeats();

        if (newBars > oldBars)
        {
            // Repeat the existing material to fill the new bars.
            std::vector<DrumNote> extra;
            for (double offset = oldLen; offset < newLen - 1.0e-6; offset += oldLen)
                for (auto n : p.notes)
                    if (n.beat < oldLen && n.beat + offset < newLen)
                    {
                        n.beat += offset;
                        extra.push_back (n);
                    }
            p.notes.insert (p.notes.end(), extra.begin(), extra.end());
        }
        else
        {
            p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(),
                                           [newLen] (const DrumNote& n) { return n.beat >= newLen; }),
                           p.notes.end());
        }
    });
}

void GrooveForgeProcessor::clearLane (int lane)
{
    modifyPattern ([lane] (Pattern& p)
    {
        p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [lane] (const DrumNote& n) { return n.lane == lane; }),
                       p.notes.end());
    });
}

//==============================================================================
void GrooveForgeProcessor::stopAllNotes (juce::MidiBuffer& midi, int sampleOffset)
{
    for (int i = 0; i < numPendingOffs; ++i)
        midi.addEvent (juce::MidiMessage::noteOff (10, pendingOffs[(size_t) i].note), sampleOffset);
    numPendingOffs = 0;
}

void GrooveForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    const juce::SpinLock::ScopedLockType sl (audioLock);

    // Incoming MIDI plays the internal kit (handy for auditioning from a keyboard).
    triggers.clear();

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            for (int l = 0; l < NumLanes; ++l)
                if (audioLanes[(size_t) l].midiNote == msg.getNoteNumber())
                    triggers.push_back ({ meta.samplePosition, l, msg.getFloatVelocity() });
    }
    midi.clear();

    if (const int mask = previewMask.exchange (0); mask != 0)
        for (int l = 0; l < NumLanes; ++l)
            if ((mask & (1 << l)) != 0)
                triggers.push_back ({ 0, l, 0.9f });

    // --- transport -------------------------------------------------------------
    bool running = false;
    double startPpq = 0.0, bpm = tempo;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto ts = pos->getTimeSignature())
            {
                hostNumerator = ts->numerator;
                hostDenominator = ts->denominator;
            }
            if (auto b = pos->getBpm())
                hostBpm = *b;

            hostIsPlaying = pos->getIsPlaying();
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition())
                {
                    running = true;
                    startPpq = *ppq;
                    bpm = pos->getBpm().orFallback (tempo);
                }
        }
    }

    if (! running && internalPlaying.load())
    {
        running = true;
        if (! wasRunning || hostIsPlaying.load())
            internalPpq = 0.0;
        startPpq = internalPpq;
    }

    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    const double beatsPerSample = bpm / 60.0 / sr;
    const double endPpq = startPpq + numSamples * beatsPerSample;

    if (! running)
    {
        if (wasRunning)
            stopAllNotes (midi, 0);
        playheadBeat = -1.0;
        lastBlockEndPpq = -1.0;
    }
    else
    {
        // transport jumped (loop / relocate): release hanging notes
        if (lastBlockEndPpq >= 0.0 && std::abs (startPpq - lastBlockEndPpq) > 0.01)
            stopAllNotes (midi, 0);

        const double len = juce::jmax (0.25, audioLength);

        // note-offs first
        for (int i = 0; i < numPendingOffs;)
        {
            auto& off = pendingOffs[(size_t) i];
            if (off.beat < endPpq)
            {
                const int s = juce::jlimit (0, numSamples - 1, (int) ((off.beat - startPpq) / beatsPerSample));
                if (sendMidiOut) midi.addEvent (juce::MidiMessage::noteOff (10, off.note), s);
                pendingOffs[(size_t) i] = pendingOffs[(size_t) --numPendingOffs];
            }
            else ++i;
        }

        for (auto& n : audioNotes)
        {
            const auto& lane = audioLanes[(size_t) n.lane];
            if (lane.muted) continue;

            double t = n.beat + std::ceil ((startPpq - n.beat) / len - 1.0e-9) * len;
            for (; t < endPpq; t += len)
            {
                if (t < startPpq) continue;
            if (triggers.size() >= triggers.capacity()) break;
                const int s = juce::jlimit (0, numSamples - 1, (int) ((t - startPpq) / beatsPerSample));
                triggers.push_back ({ s, n.lane, n.velocity });

                if (sendMidiOut)
                {
                    const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (n.velocity * 127.0f));
                    midi.addEvent (juce::MidiMessage::noteOn (10, lane.midiNote, vel), s);
                    if (numPendingOffs < (int) pendingOffs.size())
                        pendingOffs[(size_t) numPendingOffs++] = { t + juce::jmax (0.05, n.length), lane.midiNote };
                }
            }
        }

        playheadBeat = std::fmod (startPpq, len);
        lastBlockEndPpq = endPpq;
        if (! hostIsPlaying.load())
            internalPpq = endPpq;
    }

    wasRunning = running;

    // --- render audio, splitting the block at each trigger ---------------------
    std::sort (triggers.begin(), triggers.end(), [] (const Trigger& a, const Trigger& b) { return a.sample < b.sample; });

    int pos = 0;
    for (auto& tr : triggers)
    {
        if (tr.sample > pos)
        {
            synth.render (buffer, pos, tr.sample - pos, outputGain);
            pos = tr.sample;
        }
        synth.trigger (tr.lane, tr.vel);
    }
    if (pos < numSamples)
        synth.render (buffer, pos, numSamples - pos, outputGain);

    if (buffer.getNumChannels() == 1)
        buffer.applyGain (0.7f);
}

//==============================================================================
juce::String GrooveForgeProcessor::suggestedFileName() const
{
    auto genre = juce::String (Generator::info (params.genre).name)
                     .retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ")
                     .trim().replaceCharacter (' ', '_');
    return "GrooveForge_" + genre + "_" + juce::String (pattern.numerator) + "-" + juce::String (pattern.denominator)
         + "_" + juce::String (juce::roundToInt (tempo)) + "bpm.mid";
}

juce::File GrooveForgeProcessor::writeTempMidiFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GrooveForge");
    dir.createDirectory();
    auto f = dir.getChildFile (suggestedFileName());
    MidiExport::writeToFile (f, pattern, lanes, tempo, splitTracksOnExport);
    return f;
}

//==============================================================================
void GrooveForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("GrooveForge");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("num", pattern.numerator);
    xml.setAttribute ("den", pattern.denominator);
    xml.setAttribute ("grouping", pattern.grouping);
    xml.setAttribute ("bars", pattern.bars);
    xml.setAttribute ("genre", (int) params.genre);
    xml.setAttribute ("complexity", params.complexity);
    xml.setAttribute ("swing", params.swing);
    xml.setAttribute ("humanize", params.humanize);
    xml.setAttribute ("variation", params.variation);
    xml.setAttribute ("fills", params.fills);
    xml.setAttribute ("seed", params.seed);
    xml.setAttribute ("tempo", tempo);
    xml.setAttribute ("split", splitTracksOnExport);
    xml.setAttribute ("followHost", followHostMeter);
    xml.setAttribute ("snap", snapIndex);
    xml.setAttribute ("gain", outputGain);
    xml.setAttribute ("midiOut", sendMidiOut.load());

    auto* lanesXml = xml.createNewChildElement ("Lanes");
    for (int l = 0; l < NumLanes; ++l)
    {
        auto* e = lanesXml->createNewChildElement ("Lane");
        e->setAttribute ("note", lanes[(size_t) l].midiNote);
        e->setAttribute ("muted", lanes[(size_t) l].muted);
        e->setAttribute ("locked", lanes[(size_t) l].locked);
    }

    auto* notesXml = xml.createNewChildElement ("Notes");
    for (auto& n : pattern.notes)
    {
        auto* e = notesXml->createNewChildElement ("N");
        e->setAttribute ("l", n.lane);
        e->setAttribute ("b", n.beat);
        e->setAttribute ("d", n.length);
        e->setAttribute ("v", n.velocity);
    }

    copyXmlToBinary (xml, destData);
}

void GrooveForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("GrooveForge"))
        return;

    pattern.numerator = xml->getIntAttribute ("num", 4);
    pattern.denominator = xml->getIntAttribute ("den", 4);
    pattern.grouping = xml->getStringAttribute ("grouping");
    pattern.bars = juce::jlimit (1, 16, xml->getIntAttribute ("bars", 2));
    params.genre = (Genre) juce::jlimit (0, (int) Genre::NumGenres - 1, xml->getIntAttribute ("genre", 0));
    params.complexity = (float) xml->getDoubleAttribute ("complexity", 0.5);
    params.swing = (float) xml->getDoubleAttribute ("swing", 0.5);
    params.humanize = (float) xml->getDoubleAttribute ("humanize", 0.2);
    params.variation = (float) xml->getDoubleAttribute ("variation", 0.3);
    params.fills = xml->getBoolAttribute ("fills", true);
    params.seed = xml->getIntAttribute ("seed", 1);
    tempo = xml->getDoubleAttribute ("tempo", 120.0);
    splitTracksOnExport = xml->getBoolAttribute ("split", true);
    followHostMeter = xml->getBoolAttribute ("followHost", false);
    snapIndex = xml->getIntAttribute ("snap", 2);
    outputGain = (float) xml->getDoubleAttribute ("gain", 0.8);
    sendMidiOut = xml->getBoolAttribute ("midiOut", true);

    if (auto* lanesXml = xml->getChildByName ("Lanes"))
    {
        int l = 0;
        for (auto* e : lanesXml->getChildIterator())
        {
            if (l >= NumLanes) break;
            lanes[(size_t) l].midiNote = juce::jlimit (0, 127, e->getIntAttribute ("note", laneInfo (l).defaultNote));
            lanes[(size_t) l].muted = e->getBoolAttribute ("muted");
            lanes[(size_t) l].locked = e->getBoolAttribute ("locked");
            ++l;
        }
    }

    pattern.notes.clear();
    if (auto* notesXml = xml->getChildByName ("Notes"))
        for (auto* e : notesXml->getChildIterator())
            pattern.notes.push_back ({ juce::jlimit (0, NumLanes - 1, e->getIntAttribute ("l")),
                                       e->getDoubleAttribute ("b"), e->getDoubleAttribute ("d", 0.25),
                                       (float) e->getDoubleAttribute ("v", 0.8) });

    pattern.sortNotes();
    pushToAudioThread();
    sendChangeMessage();
}

juce::AudioProcessorEditor* GrooveForgeProcessor::createEditor()
{
    return new GrooveForgeEditor (*this);
}

} // namespace gf

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new gf::GrooveForgeProcessor();
}

#include "MidiExport.h"

namespace gf
{

static constexpr int ticksPerQuarter = 960;
static constexpr int drumChannel = 10;

static void addMeta (juce::MidiMessageSequence& seq, const Pattern& p, double bpm, const juce::String& name)
{
    auto trackName = juce::MidiMessage::textMetaEvent (3, name);
    trackName.setTimeStamp (0);
    seq.addEvent (trackName);

    auto tempo = juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / juce::jmax (1.0, bpm)));
    tempo.setTimeStamp (0);
    seq.addEvent (tempo);

    auto ts = juce::MidiMessage::timeSignatureMetaEvent (p.numerator, p.denominator);
    ts.setTimeStamp (0);
    seq.addEvent (ts);
}

juce::MidiFile MidiExport::createMidiFile (const Pattern& pattern,
                                           const std::array<LaneSettings, NumLanes>& lanes,
                                           double bpm, bool separateTracks, int repeats)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerQuarter);

    std::array<juce::MidiMessageSequence, NumLanes> perLane;
    juce::MidiMessageSequence all;

    const double lengthBeats = pattern.lengthInBeats();
    repeats = juce::jmax (1, repeats);

    for (int r = 0; r < repeats; ++r)
    {
        for (auto& n : pattern.notes)
        {
            const auto& lane = lanes[(size_t) n.lane];
            if (lane.muted)
                continue;

            const double start = (n.beat + r * lengthBeats) * ticksPerQuarter;
            const double end   = start + juce::jmax (0.05, n.length) * ticksPerQuarter;
            const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (n.velocity * 127.0f));

            const int key = midiNoteFor (lane, n);
            auto on  = juce::MidiMessage::noteOn  (drumChannel, key, vel);
            auto off = juce::MidiMessage::noteOff (drumChannel, key);
            on.setTimeStamp (std::round (start));
            off.setTimeStamp (std::round (end));

            auto& seq = separateTracks ? perLane[(size_t) n.lane] : all;
            seq.addEvent (on);
            seq.addEvent (off);
        }
    }

    if (separateTracks)
    {
        // Track 0: conductor track with tempo and meter only.
        juce::MidiMessageSequence conductor;
        addMeta (conductor, pattern, bpm, "Groove Forge");
        file.addTrack (conductor);

        for (int l = 0; l < NumLanes; ++l)
        {
            if (perLane[(size_t) l].getNumEvents() == 0)
                continue;

            juce::MidiMessageSequence seq;
            auto name = juce::MidiMessage::textMetaEvent (3, laneInfo (l).name);
            name.setTimeStamp (0);
            seq.addEvent (name);
            seq.addSequence (perLane[(size_t) l], 0);
            seq.updateMatchedPairs();
            seq.sort();
            file.addTrack (seq);
        }
    }
    else
    {
        juce::MidiMessageSequence seq;
        addMeta (seq, pattern, bpm, "Groove Forge Drums");
        seq.addSequence (all, 0);
        seq.updateMatchedPairs();
        seq.sort();
        file.addTrack (seq);
    }

    return file;
}

bool MidiExport::writeToFile (const juce::File& f, const Pattern& pattern,
                              const std::array<LaneSettings, NumLanes>& lanes,
                              double bpm, bool separateTracks, int repeats)
{
    auto midi = createMidiFile (pattern, lanes, bpm, separateTracks, repeats);
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (! out.openedOk())
        return false;
    return midi.writeTo (out, 1);
}

} // namespace gf

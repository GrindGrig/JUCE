#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <array>
#include <algorithm>

namespace gf
{

//==============================================================================
/** The drum layers ("lanes") of the sequencer. Each has its own General MIDI note. */
enum Lane
{
    Kick, Snare, Clap, Rim, ClosedHat, OpenHat, LowTom, MidTom, HighTom, Crash, Ride, Perc,
    Bodhran, Djembe, Cajon, Darbuka, Dundun,     // hand percussion: two strokes each
    NumLanes
};

struct LaneInfo
{
    const char* name;
    int defaultNote;          // General MIDI drum map (closest match for the hand drums)
    juce::uint32 colour;      // Cicada accent of the instrument family
    int defaultAltNote = -1;  // second stroke's note, -1 = single-stroke instrument
    const char* stroke = nullptr;     // name of the main stroke
    const char* altStroke = nullptr;  // name of the second stroke
};

inline bool hasTwoStrokes (int lane);

inline const LaneInfo& laneInfo (int lane)
{
    static const LaneInfo infos[NumLanes] =
    {
        // Cicada accents: eye = kick, gold = snare family, teal = metals, violet = deep drums, chlorophyll = hand percussion
        { "Kick",       36, 0xffff5b3a },
        { "Snare",      38, 0xffd8b25c },
        { "Clap",       39, 0xffd8b25c },
        { "Rim",        37, 0xffd8b25c },
        { "Closed Hat", 42, 0xff4fd6c4 },
        { "Open Hat",   46, 0xff4fd6c4 },
        { "Low Tom",    45, 0xffc79bff },
        { "Mid Tom",    47, 0xffc79bff },
        { "High Tom",   50, 0xffc79bff },
        { "Crash",      49, 0xff4fd6c4 },
        { "Ride",       51, 0xff4fd6c4 },
        { "Perc",       70, 0xffa6e05a },
        { "Bodhran",    66, 0xffc79bff, 65, "open", "top" },
        { "Djembe",     63, 0xffa6e05a, 62, "tone", "slap" },
        { "Cajon",      35, 0xffa6e05a, 40, "bass", "slap" },
        { "Darbuka",    61, 0xffa6e05a, 60, "doum", "tek" },
        { "Dundun",     41, 0xffc79bff, 56, "drum", "bell" },
    };
    return infos[juce::jlimit (0, NumLanes - 1, lane)];
}

inline bool hasTwoStrokes (int lane) { return laneInfo (lane).defaultAltNote >= 0; }
inline bool isHandDrum (int lane)    { return lane >= Bodhran && lane <= Dundun; }

//==============================================================================
/** A single drum hit. Positions are in quarter-note beats from the start of
    the pattern and are NOT quantised - a note may sit anywhere (off-grid). */
struct DrumNote
{
    int lane = 0;
    double beat = 0.0;
    double length = 0.25;
    float velocity = 0.8f;
    bool alt = false;     // second stroke (slap, tek, bell...) for two-stroke instruments
};

struct LaneSettings
{
    bool muted = false;
    bool locked = false;  // locked lanes are kept when generating
    int midiNote = 36;
    int altMidiNote = -1;
};

/** The MIDI note a hit sends: the lane's second-stroke note for alt hits. */
inline int midiNoteFor (const LaneSettings& lane, const DrumNote& n)
{
    return n.alt && lane.altMidiNote >= 0 ? lane.altMidiNote : lane.midiNote;
}

struct Pattern
{
    int numerator = 4;
    int denominator = 4;
    juce::String grouping;      // e.g. "2+2+3" (empty = default for the meter)
    int bars = 2;
    std::vector<DrumNote> notes;

    double beatsPerBar() const  { return numerator * 4.0 / denominator; }
    double lengthInBeats() const { return beatsPerBar() * bars; }

    void sortNotes()
    {
        std::sort (notes.begin(), notes.end(), [] (const DrumNote& a, const DrumNote& b)
        {
            if (a.beat < b.beat) return true;
            if (b.beat < a.beat) return false;
            if (a.lane != b.lane) return a.lane < b.lane;
            return a.alt < b.alt;
        });
    }
};

} // namespace gf

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
    NumLanes
};

struct LaneInfo
{
    const char* name;
    int defaultNote;      // General MIDI drum map
    juce::uint32 colour;
};

inline const LaneInfo& laneInfo (int lane)
{
    static const LaneInfo infos[NumLanes] =
    {
        { "Kick",       36, 0xffff5a5a },
        { "Snare",      38, 0xffffa23a },
        { "Clap",       39, 0xffffd43a },
        { "Rim",        37, 0xffc8e64a },
        { "Closed Hat", 42, 0xff4ae68a },
        { "Open Hat",   46, 0xff3ad6c8 },
        { "Low Tom",    45, 0xff3aa8ff },
        { "Mid Tom",    47, 0xff5a7aff },
        { "High Tom",   50, 0xff8a5aff },
        { "Crash",      49, 0xffc85aff },
        { "Ride",       51, 0xffff5ad2 },
        { "Perc",       70, 0xffb0b0b0 },
    };
    return infos[juce::jlimit (0, NumLanes - 1, lane)];
}

//==============================================================================
/** A single drum hit. Positions are in quarter-note beats from the start of
    the pattern and are NOT quantised - a note may sit anywhere (off-grid). */
struct DrumNote
{
    int lane = 0;
    double beat = 0.0;
    double length = 0.25;
    float velocity = 0.8f;
};

struct LaneSettings
{
    bool muted = false;
    bool locked = false;  // locked lanes are kept when generating
    int midiNote = 36;
};

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
            return a.lane < b.lane;
        });
    }
};

} // namespace gf

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Pattern.h"

namespace gf
{

struct MidiExport
{
    /** Builds a Standard MIDI File (960 PPQ, channel 10) with tempo and
        time-signature meta events. With separateTracks every layer gets its
        own named track, which most DAWs import as separate clips. */
    static juce::MidiFile createMidiFile (const Pattern& pattern,
                                          const std::array<LaneSettings, NumLanes>& lanes,
                                          double bpm, bool separateTracks, int repeats = 1);

    static bool writeToFile (const juce::File& file, const Pattern& pattern,
                             const std::array<LaneSettings, NumLanes>& lanes,
                             double bpm, bool separateTracks, int repeats = 1);
};

} // namespace gf

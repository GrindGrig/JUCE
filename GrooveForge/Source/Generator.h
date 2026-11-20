#pragma once

#include "Meter.h"

namespace gf
{

enum class Genre
{
    House, Techno, HipHop, Trap, DrumAndBass, Breakbeat, Rock, Metal, Funk,
    Reggae, Afrobeat, Latin, Jazz, Dubstep, Reggaeton, UKGarage,
    NumGenres
};

struct GeneratorParams
{
    Genre genre = Genre::House;
    float complexity = 0.5f;   // 0..1 density / busyness
    float swing = 0.5f;        // 0.5 = straight, 0.66 = triplet, up to 0.75
    float humanize = 0.2f;     // 0..1 off-grid timing + velocity drift
    float variation = 0.3f;    // 0..1 how much bars differ from each other
    bool fills = true;         // fill at the end of the pattern
    int seed = 1;
};

struct GenreInfo
{
    const char* name;
    double tempo;              // suggested BPM
    float swing;               // suggested swing
    bool eighthSwing;          // swing the 8th level instead of the 16th level
};

class Generator
{
public:
    static const GenreInfo& info (Genre);
    static juce::StringArray genreNames();

    /** Replaces the notes of every lane where lanesToGenerate[lane] is true
        with a freshly generated part for the pattern's meter. */
    static void generate (Pattern& pattern, const GeneratorParams& params,
                          const std::array<bool, NumLanes>& lanesToGenerate);
};

} // namespace gf

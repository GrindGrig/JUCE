#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <optional>

namespace sw
{

static constexpr int numLfos = 2;

enum LfoShape { sine = 0, triangle, sawUp, sawDown, square, sampleHold, smoothRandom, custom, numLfoShapes };

inline juce::StringArray lfoShapeNames()   { return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random S&H", "Smooth Random", "Custom" }; }
inline juce::StringArray lfoTriggerNames() { return { "Free running", "Retrigger on note" }; }

struct LfoDivision { const char* name; double beats; };

inline const std::vector<LfoDivision>& lfoDivisions()
{
    static const std::vector<LfoDivision> divs { { "4 bars", 16.0 }, { "2 bars", 8.0 }, { "1 bar", 4.0 }, { "1/2", 2.0 },
                                                 { "1/2 T", 4.0 / 3.0 }, { "1/4", 1.0 }, { "1/4 .", 1.5 }, { "1/4 T", 2.0 / 3.0 },
                                                 { "1/8", 0.5 }, { "1/8 .", 0.75 }, { "1/8 T", 1.0 / 3.0 }, { "1/16", 0.25 },
                                                 { "1/16 T", 1.0 / 6.0 }, { "1/32", 0.125 } };
    return divs;
}

inline juce::StringArray lfoDivisionNames()
{
    juce::StringArray names;

    for (const auto& d : lfoDivisions())
        names.add (d.name);

    return names;
}

/** Parameters an LFO can be routed to. The order is also the index used by the audio thread. */
inline const juce::StringArray& modTargetIds()
{
    static const juce::StringArray ids { "start", "length", "speed", "tune", "crossAmount", "envWidth", "speedB", "blur", "smear",
                                         "tonal", "tilt", "gate", "warp", "shift", "phaseRand", "sustain", "gain" };
    return ids;
}

inline const juce::StringArray& modTargetNames()
{
    static const juce::StringArray names { "Start", "Length", "Speed", "Tune", "Cross Amount", "Env Width", "B Speed", "Blur", "Smear",
                                           "Tonal", "Tilt", "Gate", "Warp", "Shift", "Diffuse", "Sustain", "Gain" };
    return names;
}

inline juce::String lfoParamId (int lfo, const char* suffix)                { return "lfo" + juce::String (lfo + 1) + suffix; }
inline juce::String modDepthId (int lfo, const juce::String& targetId)      { return "mod" + juce::String (lfo + 1) + "_" + targetId; }

//==============================================================================
/** Breakpoints of a user-drawn shape: x in [0, 1), y in [-1, 1], linear segments that wrap. */
using ShapePoints = std::vector<juce::Point<float>>;

ShapePoints defaultCustomPoints();
ShapePoints shapeToPoints (int shape);
float evaluatePoints (const ShapePoints& sortedPoints, float x);
juce::String pointsToString (const ShapePoints&);
ShapePoints pointsFromString (const juce::String&);

/** Value of a shape at a phase. With preview set, the random shapes return a fixed example pattern. */
float lfoShapeValue (int shape, float phase, const ShapePoints* customPoints, bool preview,
                     float hold = 0.0f, float prev = 0.0f, float next = 0.0f, float rawPhase = 0.0f);

/** A custom shape baked into a table the audio thread can read without locks. */
class CustomShapeTable
{
public:
    static constexpr int size = 512;

    CustomShapeTable() { setPoints (defaultCustomPoints()); }
    void setPoints (const ShapePoints& sortedPoints);
    float lookup (float phase) const noexcept;

private:
    std::array<std::atomic<float>, size> table {};
};

//==============================================================================
/** One audio-thread LFO. */
class Lfo
{
public:
    struct Settings
    {
        int    shape = sine;
        double rateHz = 1.0;
        float  phaseOffset = 0.0f;  // 0..1 of a cycle
        float  smooth = 0.0f;       // 0..1
        float  fadeSeconds = 0.0f;
        bool   unipolar = false;
    };

    void reset() noexcept { phase = 0.0; smoothed = output = 0.0f; }
    void retrigger() noexcept;

    /** Returns the output for the next numSamples. hostCyclePosition, when given, locks the
        phase to the host's musical position (in cycles of the synced division). */
    float process (const Settings&, int numSamples, double sampleRate, const CustomShapeTable&,
                   double secondsSinceNote, std::optional<double> hostCyclePosition);

    float getDisplayPhase() const noexcept  { return displayPhase.load(); }
    float getDisplayOutput() const noexcept { return displayOutput.load(); }

private:
    void newRandoms() noexcept;

    double phase = 0.0;
    float smoothed = 0.0f, output = 0.0f;
    float hold = 0.3f, prev = 0.0f, next = 0.5f;
    juce::Random random;
    std::atomic<float> displayPhase { 0.0f }, displayOutput { 0.0f };
};

} // namespace sw

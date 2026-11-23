#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Generator.h"
#include "DrumSynth.h"

namespace gf
{

/** Grid resolutions offered in the editor. "Off" = completely free placement. */
struct SnapMode
{
    const char* name;
    double beats;   // 0 = free
};

inline const std::vector<SnapMode>& snapModes()
{
    static const std::vector<SnapMode> modes =
    {
        { "1/4",   1.0 },
        { "1/8",   0.5 },
        { "1/16",  0.25 },
        { "1/32",  0.125 },
        { "1/64",  0.0625 },
        { "1/8 T", 1.0 / 3.0 },
        { "1/16 T", 1.0 / 6.0 },
        { "1/32 T", 1.0 / 12.0 },
        { "Off (free)", 0.0 },
    };
    return modes;
}

//==============================================================================
class GrooveForgeProcessor final : public juce::AudioProcessor,
                                   public juce::ChangeBroadcaster
{
public:
    GrooveForgeProcessor();
    ~GrooveForgeProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return "Groove Forge"; }
    bool acceptsMidi() const override                     { return true; }
    bool producesMidi() const override                    { return true; }
    bool isMidiEffect() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 2.0; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API used by the editor.
    const Pattern& getPattern() const                              { return pattern; }
    const std::array<LaneSettings, NumLanes>& getLanes() const     { return lanes; }
    GeneratorParams& getParams()                                   { return params; }

    void modifyPattern (const std::function<void (Pattern&)>& fn);
    void modifyLane (int lane, const std::function<void (LaneSettings&)>& fn);

    /** Generates every unlocked lane (or only `onlyLane` if >= 0). */
    void generate (int onlyLane = -1, bool newSeed = true);

    void setTimeSignature (int numerator, int denominator, const juce::String& grouping);
    void setBars (int bars);
    void clearLane (int lane);

    double tempo = 120.0;             // internal tempo (used when host isn't playing / standalone)
    bool splitTracksOnExport = true;
    bool followHostMeter = false;
    int snapIndex = 2;
    float outputGain = 0.8f;

    std::atomic<bool> internalPlaying { false };
    std::atomic<bool> sendMidiOut { true };

    // Read by the UI
    std::atomic<double> playheadBeat { -1.0 };   // position within pattern, < 0 when stopped
    std::atomic<bool> hostIsPlaying { false };
    std::atomic<int> hostNumerator { 0 }, hostDenominator { 0 };
    std::atomic<double> hostBpm { 0.0 };

    void previewLane (int lane, bool alt = false)  { previewMask.fetch_or (juce::uint64 (1) << (lane * 2 + (alt ? 1 : 0))); }

    juce::File writeTempMidiFile();
    juce::String suggestedFileName() const;

private:
    void pushToAudioThread();
    void stopAllNotes (juce::MidiBuffer& midi, int sampleOffset);

    Pattern pattern;
    std::array<LaneSettings, NumLanes> lanes;
    GeneratorParams params;

    // audio thread copies
    juce::SpinLock audioLock;
    std::vector<DrumNote> audioNotes;
    std::array<LaneSettings, NumLanes> audioLanes;
    double audioLength = 8.0;

    DrumSynth synth;
    struct Trigger { int sample; int lane; float vel; bool alt; };
    std::vector<Trigger> triggers;
    std::atomic<juce::uint64> previewMask { 0 };

    struct PendingOff { double beat; int note; };
    std::array<PendingOff, 512> pendingOffs;
    int numPendingOffs = 0;
    double internalPpq = 0.0;
    double lastBlockEndPpq = -1.0;
    bool wasRunning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveForgeProcessor)
};

} // namespace gf

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "TimbreDsp.h"

namespace ids
{
    inline const juce::String mode { "mode" }, transpose { "transpose" }, autoOctave { "autoOctave" }, snap { "snap" },
        brightness { "brightness" }, noise { "noise" }, consonants { "consonants" }, vibrato { "vibrato" }, glide { "glide" },
        grain { "grain" }, continuity { "continuity" }, pitchFix { "pitchFix" }, detail { "detail" }, flatten { "flatten" },
        air { "air" }, gate { "gate" }, mix { "mix" }, gain { "gain" }, swap { "swap" };

    /** Parameters reset by each panel's Reset button. */
    inline juce::StringArray transferSection() { return { mode, transpose, autoOctave, snap, brightness, noise, consonants, vibrato, glide, grain, continuity, pitchFix, detail, flatten, air }; }
    inline juce::StringArray outputSection()   { return { gate, mix, gain }; }
}

/** Plays one sound (A, the performance) through the character of another (B, the timbre).

    Normally A is the plug-in's input and B is a clip (built-in or a file).
    With Swap A/B on, the input becomes B: the plug-in keeps relearning its timbre from the last few
    seconds of input, and A is a clip that loops in time with the host's transport. */
class TimbalProcessor final : public juce::AudioProcessor,
                              public juce::ChangeBroadcaster,
                              private juce::Timer
{
public:
    TimbalProcessor();
    ~TimbalProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    /** Where a clip comes from: a built-in example (index >= 0) or an audio file. */
    struct ClipSource
    {
        int builtIn = 0;
        juce::File file;
        bool operator== (const ClipSource& o) const { return builtIn == o.builtIn && file == o.file; }
    };

    /** The three things the plug-in learns from. */
    enum class Slot { timbreClip = 0, performanceClip, liveTimbre };

    /** Loads a clip in the background (message thread). */
    void requestClip (Slot, const ClipSource&);
    /** Loads a clip on the calling thread and installs it at once (tests, offline use). */
    bool loadClipNow (Slot, const ClipSource&, double sampleRate);
    /** Relearns the timbre from the captured input on the calling thread (tests). */
    bool learnFromInputNow();

    ClipSource getSource (Slot s) const                     { return slots[(size_t) s].source; }
    const tt::TimbreModel* getModel (Slot s) const noexcept { return slots[(size_t) s].current.load (std::memory_order_acquire); }
    bool isLoading (Slot s) const noexcept                  { return slots[(size_t) s].loading.load(); }
    juce::String getError (Slot s) const                    { return slots[(size_t) s].error; }

    /** True when the input is the timbre (B) and A plays from a clip. */
    bool isSwapped() const noexcept { return raw.swap->load() > 0.5f; }
    /** The timbre in use right now: B's clip, or the one learned from the input when swapped. */
    const tt::TimbreModel* getActiveTimbre() const noexcept { return getModel (isSwapped() ? Slot::liveTimbre : Slot::timbreClip); }
    /** Seconds since the timbre was last learned from the input (negative if never). */
    double secondsSinceLiveLearn() const;
    /** Where the performance clip is playing, 0..1 (negative when it is not playing). */
    float getPerformancePosition() const noexcept { return clipPosition.load(); }

    /** Restores the parameters of one section to their defaults. */
    void resetParameters (const juce::StringArray& paramIds);

    juce::AudioProcessorValueTreeState parameters;
    tt::TransferEngine engine;

    static juce::StringArray modeNames() { return { "Harmonic", "Mosaic", "Vocoder" }; }
    static juce::StringArray exampleNames (Slot s) { return s == Slot::performanceClip ? tt::performanceExampleNames() : tt::builtInTimbreNames(); }

    static constexpr double liveWindowSeconds = 6.0;   // how much recent input the live timbre is learned from

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static std::vector<float> decodeFile (const juce::File&, double targetRate, juce::String& error);
    static std::unique_ptr<tt::TimbreModel> buildModel (Slot, const ClipSource&, double rate, juce::String& error);
    std::vector<float> copyCapturedInput() const;
    void startLiveLearn();
    void install (Slot, std::unique_ptr<tt::TimbreModel>);
    void timerCallback() override;
    tt::Settings readSettings() const noexcept;
    void runOnMessageThread (std::function<void (TimbalProcessor&)>);

    struct ModelSlot
    {
        std::atomic<tt::TimbreModel*> current { nullptr };
        std::unique_ptr<tt::TimbreModel> owned;
        ClipSource source;
        juce::String error;
        std::atomic<bool> loading { false };
        std::atomic<int> generation { 0 };
    };
    std::array<ModelSlot, 3> slots;
    std::vector<std::pair<std::unique_ptr<tt::TimbreModel>, juce::uint32>> retired;

    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };
    std::atomic<bool> liveLearnBusy { false };
    juce::uint32 lastLiveLearn = 0;
    int timerTicks = 0;

    double rate = 44100.0;
    std::vector<float> monoIn, performanceIn, wet, dry;
    juce::SmoothedValue<float> gainSmoothed, mixSmoothed;

    // input captured for the live timbre (audio thread writes, the learner copies)
    std::vector<float> captureRing;
    std::atomic<juce::int64> captureWritten { 0 };
    std::atomic<juce::int64> captureLoudSamples { 0 };   // samples above the gate since the last learn

    // performance clip playback
    juce::int64 freeRunPosition = 0;
    std::atomic<float> clipPosition { -1.0f };

    struct Raw
    {
        std::atomic<float>* mode; std::atomic<float>* transpose; std::atomic<float>* autoOctave; std::atomic<float>* snap;
        std::atomic<float>* brightness; std::atomic<float>* noise; std::atomic<float>* consonants; std::atomic<float>* vibrato;
        std::atomic<float>* glide; std::atomic<float>* grain; std::atomic<float>* continuity; std::atomic<float>* pitchFix;
        std::atomic<float>* detail; std::atomic<float>* flatten; std::atomic<float>* air; std::atomic<float>* gate;
        std::atomic<float>* mix; std::atomic<float>* gain; std::atomic<float>* swap;
    } raw {};

    JUCE_DECLARE_WEAK_REFERENCEABLE (TimbalProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimbalProcessor)
};

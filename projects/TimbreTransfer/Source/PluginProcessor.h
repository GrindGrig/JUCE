#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "TimbreDsp.h"

namespace ids
{
    inline const juce::String mode { "mode" }, transpose { "transpose" }, autoOctave { "autoOctave" }, snap { "snap" },
        brightness { "brightness" }, noise { "noise" }, consonants { "consonants" }, vibrato { "vibrato" }, glide { "glide" },
        grain { "grain" }, continuity { "continuity" }, pitchFix { "pitchFix" }, detail { "detail" }, flatten { "flatten" },
        air { "air" }, gate { "gate" }, mix { "mix" }, gain { "gain" };

    /** Parameters reset by each panel's Reset button. */
    inline juce::StringArray transferSection() { return { mode, transpose, autoOctave, snap, brightness, noise, consonants, vibrato, glide, grain, continuity, pitchFix, detail, flatten, air }; }
    inline juce::StringArray outputSection()   { return { gate, mix, gain }; }
}

/** Plays the live input (A) through the character of a timbre clip (B). */
class TimbreTransferProcessor final : public juce::AudioProcessor,
                                      public juce::ChangeBroadcaster,
                                      private juce::Timer
{
public:
    TimbreTransferProcessor();
    ~TimbreTransferProcessor() override;

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
    /** Where the timbre comes from: a built-in example (index >= 0) or an audio file. */
    struct TimbreSource
    {
        int builtIn = 0;
        juce::File file;
        bool operator== (const TimbreSource& o) const { return builtIn == o.builtIn && file == o.file; }
    };

    /** Loads a timbre in the background (message thread). */
    void requestTimbre (const TimbreSource&);
    /** Loads a timbre on the calling thread and installs it at once (tests, offline use). */
    bool loadTimbreNow (const TimbreSource&, double sampleRate);

    TimbreSource getTimbreSource() const { return source; }
    const tt::TimbreModel* getTimbre() const noexcept { return current.load (std::memory_order_acquire); }
    bool isLoadingTimbre() const noexcept { return loading.load(); }
    juce::String getTimbreError() const { return lastError; }

    /** Restores the parameters of one section to their defaults. */
    void resetParameters (const juce::StringArray& paramIds);

    juce::AudioProcessorValueTreeState parameters;
    tt::TransferEngine engine;

    static juce::StringArray modeNames() { return { "Harmonic", "Mosaic", "Vocoder" }; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static std::vector<float> decodeFile (const juce::File&, double targetRate, juce::String& error);
    std::unique_ptr<tt::TimbreModel> buildModel (const TimbreSource&, double rate, juce::String& error) const;
    void install (std::unique_ptr<tt::TimbreModel>);
    void timerCallback() override;
    tt::Settings readSettings() const noexcept;

    std::atomic<tt::TimbreModel*> current { nullptr };
    std::unique_ptr<tt::TimbreModel> live;
    std::vector<std::pair<std::unique_ptr<tt::TimbreModel>, juce::uint32>> retired;

    TimbreSource source;
    juce::String lastError;
    std::atomic<bool> loading { false };
    std::atomic<int> generation { 0 };
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };

    double rate = 44100.0;
    std::vector<float> monoIn, wet, dry;
    juce::SmoothedValue<float> gainSmoothed, mixSmoothed;

    struct Raw
    {
        std::atomic<float>* mode; std::atomic<float>* transpose; std::atomic<float>* autoOctave; std::atomic<float>* snap;
        std::atomic<float>* brightness; std::atomic<float>* noise; std::atomic<float>* consonants; std::atomic<float>* vibrato;
        std::atomic<float>* glide; std::atomic<float>* grain; std::atomic<float>* continuity; std::atomic<float>* pitchFix;
        std::atomic<float>* detail; std::atomic<float>* flatten; std::atomic<float>* air; std::atomic<float>* gate;
        std::atomic<float>* mix; std::atomic<float>* gain;
    } raw {};

    JUCE_DECLARE_WEAK_REFERENCEABLE (TimbreTransferProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimbreTransferProcessor)
};

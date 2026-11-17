#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "SpectralVoice.h"

/** Spectral Weaver: a MIDI instrument that resynthesises the spectral image of
    sample A and lets sample B (or the built-in controls) reshape it.
*/
class SpectralWeaverProcessor final : public juce::AudioProcessor,
                                      public juce::ChangeBroadcaster,
                                      private juce::Timer
{
public:
    enum Slot { slotA = 0, slotB = 1, numSlots = 2 };
    static constexpr int numVoices = 8;

    SpectralWeaverProcessor();
    ~SpectralWeaverProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return true; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    /** Starts analysing a file in the background. Message thread only. */
    void loadSample (int slot, const juce::File& file);
    void clearSample (int slot);

    /** Exchanges the A and B samples, together with their Normalize switches. */
    void swapSamples();

    /** Returns every parameter in a group ("playback", "cross", "spectral", "lfo", "amp") to its default. */
    void resetGroup (const juce::String& groupId);

    /** Resets every adjustable control (sample Normalize switches are left alone). */
    void resetAll();

    static juce::String normaliseParamId (int slot) { return slot == slotA ? "normA" : "normB"; }

    sw::SpectralImage::Ptr getImage (int slot) const;
    juce::String getStatus (int slot) const;
    bool isLoading (int slot) const noexcept { return loading[(size_t) slot].load(); }

    /** Normalised play positions of each active voice (negative when idle). */
    float getVoicePosition (int voice, int slot) const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::StringArray lfoShapeNames()  { return { "Sine", "Triangle", "Saw", "Square", "Random" }; }
    static juce::StringArray lfoTargetNames() { return { "Position", "Pitch", "Warp", "Shift", "Tilt", "Cross Amt", "Smear", "Blur" }; }

private:
    void timerCallback() override;
    void installImage (int slot, sw::SpectralImage::Ptr image, const juce::String& status);
    void setStatus (int slot, const juce::String& status);
    float nextLfoValue (int numSamples);
    float param (const char* id) const noexcept { return params.at (id)->load(); }

    std::map<juce::String, std::atomic<float>*> params;

    juce::Synthesiser synth;
    std::array<sw::SpectralVoice*, numVoices> voices {};
    sw::RenderParams renderParams;

    // Images shared with the audio thread. The graveyard keeps a reference to
    // everything ever installed, so the audio thread never deletes an image.
    juce::SpinLock imageLock;
    std::array<sw::SpectralImage::Ptr, numSlots> images, audioImages;
    juce::CriticalSection graveyardLock;
    juce::ReferenceCountedArray<sw::SpectralImage> graveyard;

    mutable juce::CriticalSection statusLock;
    std::array<juce::String, numSlots> status;
    std::array<std::atomic<bool>, numSlots> loading {};
    std::array<std::atomic<int>, numSlots> generation {};

    juce::AudioFormatManager formatManager;

    double currentSampleRate = 44100.0;
    double lfoPhase = 0.0;
    float  lfoRandomValue = 0.0f;
    float  modWheel = 0.0f;
    juce::Random lfoRandom;

    juce::ThreadPool analysisPool { juce::ThreadPoolOptions{}.withThreadName ("Spectral analysis").withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralWeaverProcessor)
};

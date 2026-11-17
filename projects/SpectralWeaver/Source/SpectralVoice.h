#pragma once

#include "SpectralAnalysis.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace sw
{

/** How sample B reshapes sample A. */
enum class CrossMode
{
    vocode = 0,  // B's spectral envelope imposed on A's fine structure
    multiply,    // A's bins weighted by B's bins (spectral convolution / filtering)
    morph,       // geometric interpolation between both magnitude images
    mask,        // B acts as a spectral gate on A
    imprint,     // A's magnitudes played at B's partial frequencies
    numModes
};

inline juce::StringArray crossModeNames() { return { "Vocode", "Multiply", "Morph", "Mask", "Imprint" }; }

/** Snapshot of every control a voice needs, built once per audio block. */
struct RenderParams
{
    const SpectralImage* imageA = nullptr;
    const SpectralImage* imageB = nullptr;

    // Playback
    float start = 0.0f, length = 1.0f, speed = 1.0f;
    bool  freeze = false, loop = true, keyTrack = true;
    float rootNote = 60.0f, fineSemitones = 0.0f, pitchBendRange = 2.0f;

    // Spectral sculpting
    float blur = 0.0f;       // 0..1  temporal smoothing
    float smear = 0.0f;      // 0..1  frequency smoothing
    float tonal = 0.0f;      // -1..1 >0 keeps only peaks, <0 keeps only the noise floor
    float tiltDb = 0.0f;     // dB per octave around 1 kHz
    float gateDb = -120.0f;  // bins below this (relative to the frame peak) are removed
    float warp = 1.0f;       // frequency exponent around 440 Hz (inharmonic stretch)
    float shiftHz = 0.0f;    // linear frequency shift
    float phaseRandom = 0.0f;
    float extraSemitones = 0.0f; // from the LFO

    // Cross modulation
    CrossMode crossMode = CrossMode::vocode;
    float crossAmount = 0.0f;
    int   envelopeWidth = 8;  // bins used to estimate spectral envelopes
    bool  syncB = true;
    float speedB = 1.0f;

    float gain = 1.0f;
};

/** Accepts every note on every channel. */
struct SpectralSound final : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

/** A phase-vocoder resynthesis voice that scans through a SpectralImage. */
class SpectralVoice final : public juce::SynthesiserVoice
{
public:
    SpectralVoice();

    void prepare (double sampleRate);
    void setParams (const RenderParams* p) noexcept    { params = p; }
    void setEnvelope (const juce::ADSR::Parameters& p) { adsr.setParameters (p); }

    /** Normalised play positions for the GUI; negative when idle. */
    float getDisplayPositionA() const noexcept { return displayPosA.load(); }
    float getDisplayPositionB() const noexcept { return displayPosB.load(); }

    bool canPlaySound (juce::SynthesiserSound*) override;
    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int newPitchWheelValue) override;
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override;
    using juce::SynthesiserVoice::renderNextBlock;

private:
    void synthesiseHop();
    void advancePlayheads (const SpectralImage& a, const SpectralImage* b);
    void readFrame (const SpectralImage& image, float position, float* magsOut, float* freqsOut) const;
    void applyCrossModulation (const SpectralImage& b);
    void applySculpting();
    void remapAndSynthesise();
    void finish();

    static void boxSmooth (const float* in, float* out, int radius, std::vector<double>& scratch);

    const RenderParams* params = nullptr;
    double hostRate = 44100.0;

    juce::dsp::FFT fft { Stft::order };
    std::vector<float> window, fftBuffer, ola, ready;
    std::vector<float> magA, freqA, magB, freqB, smoothed, work, envA, envB, outMag, outFreq, outBest, phase;
    std::vector<double> prefix;
    int readyPos = Stft::hop;

    juce::ADSR adsr;
    juce::Random random;

    int   midiNote = 60;
    float velocityGain = 1.0f, bendSemitones = 0.0f;
    float posA = 0.0f, posB = 0.0f;
    bool  firstHop = true, reachedEnd = false;

    std::atomic<float> displayPosA { -1.0f }, displayPosB { -1.0f };
};

} // namespace sw

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/** DSP core of TiMBAL: analysis of the timbre clip (B) and the real-time
    transfer engine that plays the live input (A) through it.

    Everything here is plain C++ on top of juce_core so the offline test runner can
    use it without a GUI. */
namespace tt
{
constexpr int maxHarmonics = 64;
constexpr int gridSize     = 96;      // log-frequency grid for timbre envelopes
constexpr float gridLowHz  = 40.0f;
constexpr float gridHighHz = 10000.0f;
constexpr double maxTimbreSeconds = 30.0;

using Envelope = std::array<float, gridSize>;

float gridFrequency (int index) noexcept;
/** Linear interpolation of an envelope (dB) at a frequency. */
float envelopeAt (const Envelope& env, float hz) noexcept;

//==============================================================================
/** In-place radix-2 complex FFT (no scaling on the inverse). */
class Fft
{
public:
    explicit Fft (int order);
    int size() const noexcept { return n; }
    void perform (double* re, double* im, bool inverse) const noexcept;

private:
    int n;
    std::vector<double> cosT, sinT;
    std::vector<int> rev;
};

/** FFT size and hop used at a given sample rate: about 46 ms frames, 75 % overlap. */
int fftOrderFor (double sampleRate) noexcept;

//==============================================================================
/** YIN pitch detector working on a box-decimated copy of the signal (about 11 kHz). */
class PitchDetector
{
public:
    void prepare (double sampleRate);

    int decimation = 4;
    int window = 300, tauMin = 10, tauMax = 230;
    double decimatedRate = 11025.0;

    /** Samples of decimated signal needed before the end index. */
    int span() const noexcept { return window + tauMax + 2; }

    /** Analyses decimated samples [end - span(), end). Returns Hz (0 if none) and sets confidence (0..1). */
    float detect (const float* decimatedEnd, float& confidence) const noexcept;

private:
    mutable std::vector<double> diff, cmnd;
};

//==============================================================================
/** Per-frame spectral features shared by the timbre analysis and the live engine. */
struct FrameAnalyser
{
    void prepare (int fftOrder, double sampleRate);

    /** Analyses frameSize samples: fills mags (fftSize/2 + 1), returns rms; sets centroid (Hz). */
    float analyse (const float* frame, float* mags, float& centroidHz) noexcept;

    std::unique_ptr<Fft> fft;
    std::vector<double> window, re, im;
    double sampleRate = 44100.0;
    int size = 2048;
};

/** Average spectrum level in each grid band, in dB. */
void bandLevels (const float* mags, int numBins, int fftSize, double sampleRate, Envelope& out) noexcept;

//==============================================================================
/** What the plug-in learns from the timbre clip. */
struct TimbreModel
{
    struct Tier
    {
        Envelope harmonic {};   // harmonic amplitude envelope (dB, loud tier peak = 0)
        Envelope noise {};      // noise floor shape (dB, max = 0)
        float noiseFraction = 0.1f;
    };

    juce::String name;
    double sampleRate = 44100.0;
    int fftSize = 2048, hop = 512, numBins = 1025;

    std::vector<float> audio;                                   // mono, at sampleRate
    std::vector<float> loudness, rms, centroid, midi, loudPct, logCentroid;
    std::vector<unsigned char> voiced;
    std::vector<float> mags;                                    // frames * numBins
    std::vector<int> active;                                    // frames above the gate

    std::array<Tier, 3> tiers;                                  // soft, medium, loud
    Envelope unvoicedNoise {};
    bool pitched = false;
    bool hasPitch = false;
    float medianMidi = 48.0f, lowMidi = 36.0f, highMidi = 60.0f;
    float gateDb = -80.0f;

    int numFrames() const noexcept { return (int) loudness.size(); }
    const float* frameMags (int f) const noexcept { return mags.data() + (size_t) f * (size_t) numBins; }
    double durationSeconds() const noexcept { return (double) audio.size() / sampleRate; }
};

/** Analyses a mono clip (already at the engine's sample rate). Runs on a background thread. */
std::unique_ptr<TimbreModel> analyseTimbre (std::vector<float> mono, double sampleRate, const juce::String& name);

/** Built-in example timbres, synthesised at the given rate. */
juce::StringArray builtInTimbreNames();
std::vector<float> synthesiseBuiltIn (int index, double sampleRate);

/** Built-in example performances, used when the live input is the timbre. */
juce::StringArray performanceExampleNames();
std::vector<float> synthesisePerformance (int index, double sampleRate);

//==============================================================================
enum class Mode { harmonic = 0, mosaic, vocoder };

struct Settings
{
    Mode mode = Mode::harmonic;
    float transpose = 0.0f;        // semitones
    bool autoOctave = true;
    float snap = 0.3f;             // 0..1
    float brightness = 1.0f;       // 0..1.5
    float noise = 1.0f;            // 0..2.5
    float consonants = 0.4f;       // 0..1
    float vibratoCents = 0.0f;
    float glideMs = 25.0f;
    int grain = 1;                 // 0 short (half a frame), 1 long (one frame)
    float continuity = 0.5f;
    float pitchFix = 1.0f;
    int bands = 32;
    float flatten = 0.8f;
    float air = 0.1f;
    float gateDb = -55.0f;
};

/** One hop of what the engine heard, for the editor. */
struct Telemetry
{
    float midi = 0.0f;          // detected input pitch (0 if unvoiced)
    float loudnessDb = -120.0f;
    float outMidi = 0.0f;       // pitch played (harmonic mode)
};

/** Single-producer, single-consumer history the audio thread writes and the editor reads. */
template <typename T, size_t Size>
struct HistoryRing
{
    std::array<T, Size> items {};
    std::atomic<int> written { 0 };

    void push (const T& t) noexcept
    {
        const int w = written.load (std::memory_order_relaxed);
        items[(size_t) w % Size] = t;
        written.store (w + 1, std::memory_order_release);
    }
};

//==============================================================================
/** Real-time timbre transfer of a mono stream. Latency is one FFT frame. */
class TransferEngine
{
public:
    TransferEngine();

    void prepare (double sampleRate);
    void reset();
    int latencySamples() const noexcept { return fftSize; }
    double getSampleRate() const noexcept { return sampleRate; }

    /** Processes n samples.
        performance: what is played (A). live: the plug-in's input, delayed into dryDelayed for the Mix control.
        When liveCarrier is true the vocoder uses the live input itself as its carrier, frame by frame, instead
        of the timbre model's recording (used when the input is B). wet gets the transferred signal. */
    void process (const float* performance, const float* live, float* wet, float* dryDelayed, int n,
                  const Settings&, const TimbreModel*, bool liveCarrier = false) noexcept;

    HistoryRing<Telemetry, 512> telemetry;
    HistoryRing<float, 16384> outputScope;   // wet output, for the spectrogram
    std::atomic<int> appliedShift { 0 };      // semitones currently applied (transpose + octave)

private:
    void runHop (const Settings&, const TimbreModel*) noexcept;
    void hopHarmonic (const Settings&, const TimbreModel&) noexcept;
    void hopMosaic (const Settings&, const TimbreModel&) noexcept;
    void hopVocoder (const Settings&, const TimbreModel&) noexcept;
    void hopVocoderLive (const Settings&) noexcept;
    void vocoderSynthesis (const Settings&, const float* carrierMags) noexcept;
    void updateBands (int count) noexcept;
    void smoothEnvelope (const float* mags, double* out) const noexcept;
    float octaveShift (const Settings&, const TimbreModel&) const noexcept;

    double sampleRate = 44100.0;
    int fftOrder = 11, fftSize = 2048, hop = 512, numBins = 1025;
    int ringSize = 8192, ringMask = 8191;

    std::vector<float> inRing, dryRing, acc, decRing, carrierMags;
    bool useLiveCarrier = false;
    int decRingSize = 4096, decMask = 4095;
    juce::int64 samplesIn = 0;
    int hopCounter = 0, decCounter = 0;
    float decSum = 0.0f;
    juce::int64 decWritten = 0;

    FrameAnalyser analyser;
    PitchDetector pitch;
    std::unique_ptr<Fft> fft;
    std::vector<double> hann, re, im, re2, im2, envA, envB;
    std::vector<float> frame, mags, decWindow, grainShort, grainLong;
    juce::Random random;

    // what the current hop heard
    float hopRms = 0.0f, hopLoud = -120.0f, hopCentroid = 0.0f, hopMidi = 48.0f, hopPct = 0.0f;
    bool hopAbove = false, hopVoiced = false;

    // live analysis state
    float peakLoud = -60.0f;
    bool wasVoiced = false;
    float pitchHist[3] {};
    int pitchHistCount = 0;
    float lastMidi = 48.0f, srcAverage = 0.0f;
    bool haveAverage = false;
    float currentOctave = 0.0f;

    // harmonic synthesis state
    std::array<float, maxHarmonics> prevAmps {}, curAmps {};
    float prevHz = 110.0f, curHz = 110.0f, glideMidi = 48.0f;
    float voicing = 0.0f, prevAge = 0.0f, curAge = 0.0f;
    int voicedRun = 0;
    double phase = 0.0, vibPhase = 0.0;
    int prevK = 0, curK = 0;
    Envelope env {}, noiseShape {}, own {};

    // mosaic and vocoder state
    int previousGrain = -1;
    juce::int64 vocoderStep = 0;
    struct Band { int lo, hi; float centre; };
    std::vector<Band> bandList;
    mutable std::vector<double> bandValues;
    int bandCount = -1;
};

} // namespace tt

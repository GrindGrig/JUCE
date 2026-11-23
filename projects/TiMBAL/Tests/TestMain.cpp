// Offline checks for TiMBAL.
//
//   TimbalTests                    run the DSP checks (exit code 1 on failure)
//   TimbalTests --wav <dir>        also write the input and each method's output as WAV files
//   TimbalTests --screenshot <png> render the editor after feeding it audio (needs a display, e.g. xvfb-run)
//                [--swapped]                     ... with Swap A/B on: a cello on the input, the hummed melody clip performing

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr double rate = 48000.0;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  pass  " : "  FAIL  ") << what << std::endl;
        if (! ok) ++failures;
    }

    float rmsOf (const std::vector<float>& x, size_t from = 0)
    {
        double s = 0.0;
        for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
        return (float) std::sqrt (s / (double) juce::jmax ((size_t) 1, x.size() - from));
    }

    /** A hummed melody: a glottal-like harmonic tone through a vowel formant, with rests. */
    std::vector<float> hummedMelody (double sr)
    {
        const float notes[] = { 57, 60, 62, 64, -1, 62, 60, 59, 57 };
        const float noteSec = 0.45f;
        std::vector<float> out;
        double ph = 0.0;
        for (float m : notes)
        {
            const int len = (int) (noteSec * sr);
            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / (float) sr;
                const float env = m < 0 ? 0.0f : juce::jmin (1.0f, t / 0.04f) * juce::jmin (1.0f, (noteSec - t) / 0.06f);
                const double f = m < 0 ? 220.0 : 440.0 * std::pow (2.0, (m - 69.0) / 12.0) * (1.0 + 0.004 * std::sin (6.28318 * 5.2 * t));
                ph += 6.283185307 * f / sr;
                double s = 0.0;
                for (int k = 1; k * f < sr * 0.45 && k <= 30; ++k)
                {
                    const double fk = k * f, formant = 1.0 / (1.0 + std::pow ((fk - 600.0) / 220.0, 2.0)) + 0.4 / (1.0 + std::pow ((fk - 1000.0) / 300.0, 2.0));
                    s += std::sin (k * ph) * std::pow (k, -1.2) * (0.05 + formant);
                }
                out.push_back ((float) (0.25 * env * s));
            }
        }
        return out;
    }

    /** Median detected pitch (MIDI) of a signal, using the plug-in's own detector. */
    float medianPitch (const std::vector<float>& x, double sr)
    {
        tt::PitchDetector pd;
        pd.prepare (sr);
        const int D = pd.decimation;
        std::vector<float> dec (x.size() / (size_t) D);
        for (size_t k = 0; k < dec.size(); ++k)
        {
            float s = 0.0f;
            for (int q = 0; q < D; ++q) s += x[k * (size_t) D + (size_t) q];
            dec[k] = s / (float) D;
        }
        std::vector<float> found;
        for (size_t end = (size_t) pd.span(); end < dec.size(); end += 64)
        {
            float conf = 0.0f;
            const float f = pd.detect (dec.data() + end, conf);
            if (conf > 0.85f && f > 40.0f) found.push_back (69.0f + 12.0f * std::log2 (f / 440.0f));
        }
        if (found.empty()) return 0.0f;
        std::sort (found.begin(), found.end());
        return found[found.size() / 2];
    }

    void writeWav (const juce::File& file, const std::vector<float>& x, double sr)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (16)))
        {
            const float* ch[] = { x.data() };
            writer->writeFromFloatArrays (ch, 1, (int) x.size());
        }
    }

    int runDspChecks (const juce::File& wavDir)
    {
        std::cout << "Timbre analysis" << std::endl;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        auto cello = tt::analyseTimbre (tt::synthesiseBuiltIn (0, rate), rate, "Cello");
        std::cout << "    analysed " << juce::String (cello->durationSeconds(), 1) << " s in "
                  << juce::String (juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - start)) << " ms" << std::endl;
        check (cello->pitched && cello->hasPitch, "the cello has a steady pitch");
        check (cello->medianMidi > 45.0f && cello->medianMidi < 62.0f, "cello median pitch " + juce::String (cello->medianMidi, 1) + " is in its range");
        check (cello->tiers[2].noiseFraction < 0.3f, "loud cello frames are mostly harmonic");

        // the loud tier should be brighter than the soft tier above 1 kHz
        float soft = 0.0f, loud = 0.0f;
        for (int g = 0; g < tt::gridSize; ++g)
            if (tt::gridFrequency (g) > 1000.0f) { soft += cello->tiers[0].harmonic[(size_t) g]; loud += cello->tiers[2].harmonic[(size_t) g]; }
        check (loud > soft, "loud tier is brighter than the soft tier");

        for (int i = 1; i < 4; ++i)
        {
            auto m = tt::analyseTimbre (tt::synthesiseBuiltIn (i, rate), rate, tt::builtInTimbreNames()[i]);
            check (m->hasPitch && ! m->active.empty(), tt::builtInTimbreNames()[i] + " analyses with a pitch");
        }

        const auto input = hummedMelody (rate);
        const float inPitch = medianPitch (input, rate);
        std::cout << "Live engine (hummed melody, median " << juce::String (inPitch, 1) << ")" << std::endl;
        if (wavDir != juce::File()) writeWav (wavDir.getChildFile ("input-hum.wav"), input, rate);

        const char* names[] = { "harmonic", "mosaic", "vocoder" };
        for (int mode = 0; mode < 3; ++mode)
        {
            tt::TransferEngine engine;
            engine.prepare (rate);
            tt::Settings s;
            s.mode = (tt::Mode) mode;

            std::vector<float> wet (input.size()), dry (input.size());
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (size_t pos = 0; pos < input.size(); pos += 480)
            {
                const int n = (int) juce::jmin ((size_t) 480, input.size() - pos);
                engine.process (input.data() + pos, input.data() + pos, wet.data() + pos, dry.data() + pos, n, s, cello.get());
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const double realtime = (double) input.size() / rate * 1000.0 / ms;

            bool finite = true;
            for (auto v : wet) finite = finite && std::isfinite (v);
            const float ratio = rmsOf (wet) / rmsOf (input);
            std::cout << "  " << names[mode] << ": " << juce::String (juce::roundToInt (realtime)) << "x real time, level ratio " << juce::String (ratio, 2) << std::endl;
            check (finite, juce::String (names[mode]) + " output is finite");
            check (ratio > 0.2f && ratio < 4.0f, juce::String (names[mode]) + " output level follows the input");

            // dry path is the input delayed by exactly the latency
            const int lat = engine.latencySamples();
            float err = 0.0f;
            for (size_t i = (size_t) lat; i < input.size(); ++i) err = juce::jmax (err, std::abs (dry[i] - input[i - (size_t) lat]));
            check (err < 1.0e-6f, juce::String (names[mode]) + " dry signal is delayed by the reported latency");

            if (mode == 0)
            {
                const float outPitch = medianPitch (wet, rate);
                const float expected = inPitch + (float) engine.appliedShift.load();
                check (std::abs (outPitch - expected) < 0.6f, "harmonic output plays A's pitch (" + juce::String (outPitch, 1) + " vs " + juce::String (expected, 1) + ")");
                check (realtime > 10.0, "harmonic mode runs comfortably in real time");
            }

            if (wavDir != juce::File()) writeWav (wavDir.getChildFile (juce::String ("hum-to-cello-") + names[mode] + ".wav"), wet, rate);
        }

        // silence in, silence out
        {
            tt::TransferEngine engine;
            engine.prepare (rate);
            std::vector<float> zeros (48000, 0.0f), wet (48000), dry (48000);
            engine.process (zeros.data(), zeros.data(), wet.data(), dry.data(), 48000, {}, cello.get());
            check (juce::exactlyEqual (rmsOf (wet), 0.0f), "silence in gives silence out");
        }

        // Swap A/B: the input (a cello) is the timbre, the hummed melody clip performs
        std::cout << "Swapped roles (cello on the input, hummed melody clip performing)" << std::endl;
        {
            using Slot = TimbalProcessor::Slot;
            const auto celloIn = tt::synthesiseBuiltIn (0, rate);
            const auto humClip = tt::synthesisePerformance (0, rate);
            const float humPitch = medianPitch (humClip, rate);

            for (int mode : { 0, 2 })
            {
                TimbalProcessor processor;
                processor.setPlayConfigDetails (2, 2, rate, 512);
                processor.prepareToPlay (rate, 512);
                processor.parameters.getParameter (ids::swap)->setValueNotifyingHost (1.0f);
                processor.parameters.getParameter (ids::mode)->setValueNotifyingHost ((float) mode / 2.0f);
                // a fixed octave, so the output pitch can be compared with the clip's
                processor.parameters.getParameter (ids::autoOctave)->setValueNotifyingHost (0.0f);
                check (processor.loadClipNow (Slot::performanceClip, { 0, {} }, rate), "performance clip loads");

                juce::AudioBuffer<float> buffer (2, 512);
                juce::MidiBuffer midi;
                std::vector<float> out;
                bool learned = false;
                for (size_t pos = 0; pos + 512 <= celloIn.size(); pos += 512)
                {
                    for (int c = 0; c < 2; ++c) buffer.copyFrom (c, 0, celloIn.data() + pos, 512);
                    processor.processBlock (buffer, midi);
                    if (! learned && pos >= (size_t) (rate * 6.0)) learned = processor.learnFromInputNow();
                    if (learned) out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
                }

                const auto* live = processor.getModel (Slot::liveTimbre);
                bool finite = true;
                for (auto v : out) finite = finite && std::isfinite (v);
                check (learned && live != nullptr && live->hasPitch, "the timbre is learned from the input");
                check (finite && rmsOf (out) > 0.01f, juce::String (mode == 0 ? "harmonic" : "vocoder") + " output with swapped roles is audible and finite");

                if (mode == 0)
                {
                    const float outPitch = medianPitch (out, rate), expected = humPitch + (float) processor.engine.appliedShift.load();
                    check (std::abs (outPitch - expected) < 0.6f, "swapped output plays the clip's pitch (" + juce::String (outPitch, 1) + " vs " + juce::String (expected, 1) + ")");
                    if (wavDir != juce::File()) writeWav (wavDir.getChildFile ("swapped-hum-clip-through-live-cello.wav"), out, rate);
                }
            }
        }

        std::cout << (failures == 0 ? "All checks passed" : juce::String (failures) + " check(s) failed") << std::endl;
        return failures == 0 ? 0 : 1;
    }

    /** Feeds the hummed melody through the processor block by block while the editor's timers run,
        then saves a snapshot of the editor. */
    class ScreenshotRun final : private juce::Timer
    {
    public:
        ScreenshotRun (const juce::File& file, bool swappedRoles, std::function<void()> done)
            : png (file), onDone (std::move (done)), swapped (swappedRoles)
        {
            using Slot = TimbalProcessor::Slot;
            processor.setPlayConfigDetails (2, 2, rate, 512);
            processor.prepareToPlay (rate, 512);
            processor.loadClipNow (Slot::timbreClip, { 0, {} }, rate);
            processor.loadClipNow (Slot::performanceClip, { 0, {} }, rate);
            if (swapped) processor.parameters.getParameter (ids::swap)->setValueNotifyingHost (1.0f);
            editor.reset (processor.createEditorAndMakeActive());
            // normally a hummed melody is the input; swapped, a cello is the input and the hum is the clip
            input = swapped ? tt::synthesiseBuiltIn (0, rate) : hummedMelody (rate);
            startTimer (10);
        }

    private:
        void timerCallback() override
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (int b = 0; b < 8; ++b)
            {
                if (pos + 512 > input.size()) { pos = 0; ++rounds; }
                for (int c = 0; c < 2; ++c) buffer.copyFrom (c, 0, input.data() + pos, 512);
                processor.processBlock (buffer, midi);
                pos += 512;
            }

            if (swapped && ! learned && pos > (size_t) (rate * 6.0)) learned = processor.learnFromInputNow();
            if (rounds < (swapped ? 1 : 2)) return;
            if (++settle < 15) return;   // let the editor's 30 Hz timer catch up

            stopTimer();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            png.deleteFile();
            juce::FileOutputStream out (png);
            juce::PNGImageFormat().writeImageToStream (image, out);
            std::cout << "Wrote " << png.getFullPathName() << std::endl;
            editor.reset();
            onDone();
        }

        juce::File png;
        std::function<void()> onDone;
        TimbalProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        std::vector<float> input;
        size_t pos = 0;
        int rounds = 0, settle = 0;
        bool swapped = false, learned = false;
    };
}

class TestApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "TimbalTests"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String&) override
    {
        const auto args = getCommandLineParameterArray();
        juce::File wavDir, shot;
        for (int i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] == "--wav") wavDir = juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1]);
            if (args[i] == "--screenshot") shot = juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1]);
        }
        if (wavDir != juce::File()) wavDir.createDirectory();

        if (shot != juce::File())
        {
            screenshot = std::make_unique<ScreenshotRun> (shot, args.contains ("--swapped"), [this] { quit(); });
            return;
        }

        setApplicationReturnValue (runDspChecks (wavDir));
        quit();
    }

    void shutdown() override { screenshot.reset(); }

private:
    std::unique_ptr<ScreenshotRun> screenshot;
};

START_JUCE_APPLICATION (TestApp)

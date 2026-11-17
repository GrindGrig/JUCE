// Offline checks for the analysis/resynthesis engine. Build with
// -DSPECTRAL_WEAVER_BUILD_TESTS=ON and run SpectralWeaverTests.

#include "SpectralVoice.h"
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        failures += ok ? 0 : 1;
    }

    std::vector<float> makeTone (double hz, double seconds, float amp, bool saw = false)
    {
        std::vector<float> s ((size_t) (seconds * sw::Stft::sampleRate));

        for (size_t i = 0; i < s.size(); ++i)
        {
            const double t = (double) i / sw::Stft::sampleRate;

            if (! saw)
            {
                s[i] = amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * t);
            }
            else
            {
                double v = 0.0;
                for (int h = 1; h * hz < 16000.0; ++h)
                    v += std::sin (juce::MathConstants<double>::twoPi * hz * h * t) / h;
                s[i] = amp * 0.5f * (float) v;
            }
        }

        return s;
    }

    struct Result { float rms = 0, freq = 0; bool finite = true; };

    Result render (sw::RenderParams& rp, double hostRate, int note, double seconds, int blockSize = 256)
    {
        juce::Synthesiser synth;
        auto* voice = new sw::SpectralVoice();
        voice->prepare (hostRate);
        voice->setParams (&rp);
        voice->setEnvelope ({ 0.001f, 0.1f, 1.0f, 0.05f });
        synth.addVoice (voice);
        synth.addSound (new sw::SpectralSound());
        synth.setCurrentPlaybackSampleRate (hostRate);

        const int total = (int) (seconds * hostRate);
        juce::AudioBuffer<float> out (1, total);
        out.clear();

        for (int pos = 0; pos < total; pos += blockSize)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
            synth.renderNextBlock (out, midi, pos, juce::jmin (blockSize, total - pos));
        }

        // Analyse the steady-state second half
        const float* d = out.getReadPointer (0);
        const int from = total / 2;
        double sq = 0.0; int crossings = 0;
        Result r;

        for (int i = from; i < total; ++i)
        {
            r.finite = r.finite && std::isfinite (d[i]);
            sq += (double) d[i] * d[i];
            if (i > from && d[i - 1] < 0.0f && d[i] >= 0.0f)
                ++crossings;
        }

        r.rms  = (float) std::sqrt (sq / (total - from));
        r.freq = (float) (crossings / ((total - from) / hostRate));
        return r;
    }

    // Writes two demo samples plus a base64 plug-in state referencing them
    int writeDemo (const juce::File& dir)
    {
        dir.createDirectory();
        const double sr = 48000.0;
        const int len = (int) (sr * 6.0);
        juce::Random rng (42);

        juce::AudioBuffer<float> pad (1, len), voice (1, len);
        pad.clear(); voice.clear();

        // A: an evolving chord with a bright attack, vibrato and a noise swell
        const double chord[] = { 130.81, 164.81, 196.0, 246.94, 329.63 };
        for (int i = 0; i < len; ++i)
        {
            const double t = i / sr;
            const double bright = 0.15 + 0.85 * (0.5 + 0.5 * std::sin (t * 1.3));
            double v = 0.0;
            for (auto f0 : chord)
                for (int h = 1; h <= 24 && f0 * h < 18000.0; ++h)
                    v += std::sin (juce::MathConstants<double>::twoPi * f0 * h * t * (1.0 + 0.003 * std::sin (t * 5.0)))
                         * std::pow ((double) h, -1.0 - 1.5 * (1.0 - bright)) * 0.15;
            const double noise = (rng.nextFloat() * 2.0 - 1.0) * 0.05 * std::pow (std::sin (juce::MathConstants<double>::pi * t / 6.0), 4.0);
            pad.setSample (0, i, (float) ((v + noise) * std::min (1.0, t * 8.0)));
        }

        // B: a pulse train through moving vowel formants (a "talking" modulator)
        double phase = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double t = i / sr;
            const double f0 = 98.0 * (1.0 + 0.25 * std::sin (t * 0.7));
            phase += f0 / sr;
            const double vowel = 0.5 + 0.5 * std::sin (t * 2.1);
            const double formants[] = { 300 + 500 * vowel, 900 + 1300 * (1 - vowel), 2600 };
            double v = 0.0;
            for (int h = 1; f0 * h < 8000.0; ++h)
            {
                double g = 0.0;
                for (auto F : formants)
                    g += 1.0 / (1.0 + std::pow ((f0 * h - F) / 120.0, 2.0));
                v += std::sin (juce::MathConstants<double>::twoPi * h * phase) * g;
            }
            const double syllable = std::pow (0.5 + 0.5 * std::sin (t * juce::MathConstants<double>::twoPi * 1.5), 0.6);
            voice.setSample (0, i, (float) (v * 0.08 * syllable));
        }

        const auto write = [&] (const juce::AudioBuffer<float>& buf, const juce::String& name, double rate)
        {
            auto file = dir.getChildFile (name);
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
            if (writer == nullptr) return juce::File();
            auto copy = buf;
            copy.applyGain (0.8f / juce::jmax (1.0e-6f, copy.getMagnitude (0, 0, copy.getNumSamples())));
            writer->writeFromAudioSampleBuffer (copy, 0, copy.getNumSamples());
            return file;
        };

        const auto a = write (pad, "DemoPad.wav", sr);
        const auto b = write (voice, "DemoVoice.wav", 44100.0); // exercises the resampler

        // Round-trip both files through the real file analyser
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        for (const auto& f : { a, b })
        {
            juce::String error;
            auto image = sw::analyseFile (f, formats, error);
            if (image == nullptr) { std::fprintf (stderr, "analyseFile failed: %s\n", error.toRawUTF8()); return 1; }
            std::fprintf (stderr, "%s: %.2fs, %d frames\n", image->name.toRawUTF8(), image->lengthSeconds, image->numFrames);
        }

        juce::XmlElement xml ("SpectralWeaver");
        xml.setAttribute ("fileA", a.getFullPathName());
        xml.setAttribute ("fileB", b.getFullPathName());

        juce::MemoryBlock state;
        {
            juce::MemoryOutputStream out (state, false);
            out.writeInt (0x21324356);
            out.writeInt (0);
            xml.writeTo (out, juce::XmlElement::TextFormat().singleLine());
            out.writeByte (0);
        }
        static_cast<juce::uint32*> (state.getData())[1] = (juce::uint32) state.getSize() - 9;
        std::printf ("%s\n", state.toBase64Encoding().toRawUTF8());
        return 0;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc == 3 && juce::String (argv[1]) == "--write-demo")
        return writeDemo (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    // 1. Analysis: instantaneous frequency of a 440 Hz tone
    auto tone = makeTone (440.0, 2.0, 0.5f);
    auto a = sw::analyseSamples (tone.data(), (int) tone.size());
    {
        const int f = a->numFrames / 2;
        const auto* m = a->mag (f);
        int best = 0;
        for (int k = 1; k < sw::Stft::numBins; ++k) if (m[k] > m[best]) best = k;
        const float hz = a->freq (f)[best];
        check (std::abs (hz - 440.0f) < 1.0f, "analysis inst. frequency = " + juce::String (hz, 2) + " Hz (expected 440)");
        check (a->picture.isValid(), "spectrogram picture rendered");
    }

    sw::RenderParams rp;
    rp.imageA = a.get();
    rp.gain = 1.0f;

    // 2. Identity resynthesis at 48 kHz
    {
        const auto r = render (rp, 48000.0, 60, 1.0);
        check (r.finite && std::abs (r.freq - 440.0f) < 3.0f, "resynthesis pitch = " + juce::String (r.freq, 1) + " Hz (expected 440)");
        const float expected = 0.5f / std::sqrt (2.0f);
        check (std::abs (r.rms / expected - 1.0f) < 0.1f, "resynthesis level = " + juce::String (r.rms, 4) + " rms (expected " + juce::String (expected, 4) + ")");
    }

    // 2b. Normalize gain scales the output linearly
    {
        auto p = rp;
        p.levelA = 2.0f;
        const auto r = render (p, 48000.0, 60, 1.0);
        check (std::abs (r.rms / (0.5f / std::sqrt (2.0f)) - 2.0f) < 0.2f, "level gain x2 = " + juce::String (r.rms, 4) + " rms (expected ~0.707)");
    }

    // 3. Key tracking: one octave up, and a different host sample rate
    {
        const auto r = render (rp, 44100.0, 72, 1.0);
        check (r.finite && std::abs (r.freq - 880.0f) < 6.0f, "octave up @44.1k = " + juce::String (r.freq, 1) + " Hz (expected 880)");
        const auto r2 = render (rp, 96000.0, 48, 1.0);
        check (r2.finite && std::abs (r2.freq - 220.0f) < 3.0f, "octave down @96k = " + juce::String (r2.freq, 1) + " Hz (expected 220)");
    }

    // 4. Every sculpting control at extreme settings stays finite
    {
        auto p = rp;
        p.blur = 0.9f; p.smear = 0.6f; p.tonal = 0.8f; p.tiltDb = 9.0f; p.gateDb = -40.0f;
        p.warp = 1.6f; p.shiftHz = 300.0f; p.phaseRandom = 0.5f; p.speed = -2.0f;
        const auto r = render (p, 48000.0, 64, 1.0);
        check (r.finite && r.rms > 1.0e-4f, "extreme sculpting finite, rms " + juce::String (r.rms, 4));
        p.freeze = true; p.start = 0.5f; p.loop = false;
        const auto r2 = render (p, 48000.0, 64, 1.0);
        check (r2.finite && r2.rms > 1.0e-4f, "frozen playback finite, rms " + juce::String (r2.rms, 4));
    }

    // 5. Cross modulation modes with a saw as B
    auto sawSamples = makeTone (110.0, 3.0, 0.5f, true);
    auto b = sw::analyseSamples (sawSamples.data(), (int) sawSamples.size());
    {
        auto p = rp;
        p.imageB = b.get();
        p.crossAmount = 1.0f;

        for (int m = 0; m < (int) sw::CrossMode::numModes; ++m)
        {
            p.crossMode = (sw::CrossMode) m;
            p.syncB = (m % 2) == 0;
            const auto r = render (p, 48000.0, 60, 1.0, 97);
            check (r.finite && r.rms < 4.0f, "cross mode " + sw::crossModeNames()[m] + " finite, rms " + juce::String (r.rms, 4));
        }

        auto sine110 = makeTone (110.0, 3.0, 0.5f);
        auto bSine = sw::analyseSamples (sine110.data(), (int) sine110.size());
        p.imageB = bSine.get();
        p.crossMode = sw::CrossMode::morph;
        const auto r = render (p, 48000.0, 60, 1.0);
        check (std::abs (r.freq - 110.0f) < 15.0f, "morph at 100% follows B's pitch: " + juce::String (r.freq, 1) + " Hz (expected ~110)");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

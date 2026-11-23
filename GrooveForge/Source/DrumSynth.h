#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Pattern.h"

namespace gf
{

/** A tiny synthesized drum kit so patterns are audible without loading samples.
    One voice per lane and stroke (re-triggering restarts it), so a dundun's drum and
    bell can sound together; closed hat chokes open hat. */
class DrumSynth
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        for (int l = 0; l < NumLanes; ++l)
            for (int a = 0; a < 2; ++a)
            {
                auto& v = voices[(size_t) (l * 2 + a)];
                v.lane = l;
                v.alt = a == 1;
                v.setup (sampleRate);
            }
    }

    void trigger (int lane, float velocity, bool alt = false)
    {
        if (! juce::isPositiveAndBelow (lane, (int) NumLanes))
            return;
        if (lane == ClosedHat)
            voices[(size_t) (OpenHat * 2)].choke();
        voices[(size_t) (lane * 2 + (alt && hasTwoStrokes (lane) ? 1 : 0))].start (velocity);
    }

    /** Renders [start, start + num) adding into the buffer. */
    void render (juce::AudioBuffer<float>& buffer, int start, int num, float gain)
    {
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

        for (auto& v : voices)
        {
            if (! v.active) continue;
            for (int i = start; i < start + num; ++i)
            {
                const float s = v.next() * gain;
                left[i] += s * v.panL;
                if (right != nullptr) right[i] += s * v.panR;
                if (! v.active) break;
            }
        }
    }

private:
    struct Voice
    {
        int lane = 0;
        bool alt = false;
        bool active = false;
        double sr = 44100.0;
        double t = 0.0;            // seconds since trigger
        double phase = 0.0;
        float vel = 1.0f;
        float panL = 1.0f, panR = 1.0f;
        float chokeGain = 1.0f;
        bool choking = false;
        juce::Random noise;
        juce::IIRFilter filterA, filterB;
        double metalPhases[6] {};

        void setup (double rate)
        {
            sr = rate;
            float pan = 0.0f;
            switch (lane)
            {
                case ClosedHat: case OpenHat: pan = 0.25f; break;
                case HighTom:   pan = -0.3f; break;
                case MidTom:    pan = -0.1f; break;
                case LowTom:    pan = 0.2f; break;
                case Ride:      pan = 0.35f; break;
                case Crash:     pan = -0.35f; break;
                case Perc:      pan = 0.45f; break;
                case Bodhran:   pan = -0.2f; break;
                case Djembe:    pan = 0.3f; break;
                case Darbuka:   pan = -0.35f; break;
                case Dundun:    pan = 0.15f; break;
                default: break;
            }
            panL = std::sqrt (0.5f * (1.0f - pan)) * 1.41f;
            panR = std::sqrt (0.5f * (1.0f + pan)) * 1.41f;

            auto hp = [&] (double f) { return juce::IIRCoefficients::makeHighPass (sr, f); };
            auto bp = [&] (double f, double q) { return juce::IIRCoefficients::makeBandPass (sr, f, q); };

            switch (lane)
            {
                case Snare:     filterA.setCoefficients (hp (1800.0)); break;
                case Clap:      filterA.setCoefficients (bp (1200.0, 1.2)); break;
                case Rim:       filterA.setCoefficients (bp (2200.0, 4.0)); break;
                case ClosedHat:
                case OpenHat:   filterA.setCoefficients (hp (7000.0)); filterB.setCoefficients (hp (7000.0)); break;
                case Crash:     filterA.setCoefficients (hp (4000.0)); break;
                case Ride:      filterA.setCoefficients (bp (5200.0, 1.5)); break;
                case Perc:      filterA.setCoefficients (bp (6500.0, 1.8)); break;
                case Bodhran:   filterA.setCoefficients (alt ? bp (1600.0, 1.5) : juce::IIRCoefficients::makeLowPass (sr, 900.0)); break;
                case Djembe:    filterA.setCoefficients (alt ? bp (2600.0, 0.9) : bp (700.0, 1.0)); break;
                case Cajon:     filterA.setCoefficients (alt ? hp (1400.0) : juce::IIRCoefficients::makeLowPass (sr, 600.0)); break;
                case Darbuka:   filterA.setCoefficients (bp (3800.0, 2.0)); break;
                case Dundun:    filterA.setCoefficients (alt ? bp (900.0, 2.0) : bp (1200.0, 1.0)); break;
                default:        filterA.setCoefficients (hp (20.0)); break;
            }
        }

        void start (float velocity)
        {
            active = true;
            choking = false;
            chokeGain = 1.0f;
            t = 0.0;
            phase = 0.0;
            vel = juce::jlimit (0.0f, 1.0f, velocity);
            filterA.reset();
            filterB.reset();
            for (auto& p : metalPhases) p = 0.0;
        }

        void choke() { if (active) choking = true; }

        float white() { return noise.nextFloat() * 2.0f - 1.0f; }

        float bell()   // two-partial cowbell-like tone
        {
            metalPhases[0] += 562.0 / sr; if (metalPhases[0] >= 1.0) metalPhases[0] -= 1.0;
            metalPhases[1] += 845.0 / sr; if (metalPhases[1] >= 1.0) metalPhases[1] -= 1.0;
            return ((metalPhases[0] < 0.5 ? 1.0f : -1.0f) + (metalPhases[1] < 0.5 ? 1.0f : -1.0f)) * 0.5f;
        }

        /** Membrane with a downward pitch glide: from `from` Hz to `to` Hz over `glide` s. */
        float drum (double from, double to, double glide)
        {
            return tone (to + (from - to) * std::exp (-t / glide));
        }

        float metal()
        {
            static const double ratios[6] = { 1.0, 1.342, 1.2312, 1.6532, 1.9523, 2.1523 };
            float s = 0.0f;
            for (int i = 0; i < 6; ++i)
            {
                metalPhases[i] += 330.0 * ratios[i] / sr;
                if (metalPhases[i] >= 1.0) metalPhases[i] -= 1.0;
                s += metalPhases[i] < 0.5 ? 1.0f : -1.0f;
            }
            return s / 6.0f;
        }

        float tone (double freq)
        {
            phase += freq / sr;
            if (phase >= 1.0) phase -= 1.0;
            return (float) std::sin (phase * juce::MathConstants<double>::twoPi);
        }

        static float env (double time, double decay) { return (float) std::exp (-time / decay); }

        float next()
        {
            float out = 0.0f;
            double length = 0.5;

            switch (lane)
            {
                case Kick:
                {
                    const double f = 48.0 + 110.0 * std::exp (-t / 0.035);
                    out = tone (f) * env (t, 0.32) + white() * env (t, 0.003) * 0.3f;
                    out = std::tanh (out * 1.6f);
                    length = 1.4;
                    break;
                }
                case Snare:
                    out = tone (185.0 + 40.0 * std::exp (-t / 0.02)) * env (t, 0.06) * 0.6f
                        + filterA.processSingleSampleRaw (white()) * env (t, 0.16) * 0.8f;
                    length = 0.8;
                    break;
                case Clap:
                {
                    const double burst = std::fmod (t, 0.011);
                    const float bursts = t < 0.033 ? env (burst, 0.004) : 0.0f;
                    const float tail = t >= 0.022 ? env (t - 0.022, 0.13) : 0.0f;
                    out = filterA.processSingleSampleRaw (white()) * (bursts + tail) * 1.6f;
                    length = 0.8;
                    break;
                }
                case Rim:
                    out = filterA.processSingleSampleRaw (white()) * env (t, 0.012) * 1.6f + tone (820.0) * env (t, 0.02) * 0.5f;
                    length = 0.2;
                    break;
                case ClosedHat:
                    out = filterB.processSingleSampleRaw (filterA.processSingleSampleRaw (white() * 0.6f + metal() * 0.5f)) * env (t, 0.035);
                    length = 0.25;
                    break;
                case OpenHat:
                    out = filterB.processSingleSampleRaw (filterA.processSingleSampleRaw (white() * 0.6f + metal() * 0.5f)) * env (t, 0.32) * 0.8f;
                    length = 1.8;
                    break;
                case LowTom:
                case MidTom:
                case HighTom:
                {
                    const double base = lane == LowTom ? 85.0 : lane == MidTom ? 120.0 : 165.0;
                    out = tone (base * (1.0 + 0.6 * std::exp (-t / 0.04))) * env (t, 0.28) * 0.9f
                        + white() * env (t, 0.004) * 0.2f;
                    length = 1.4;
                    break;
                }
                case Crash:
                    out = filterA.processSingleSampleRaw (white() * 0.7f + metal() * 0.6f) * env (t, 1.1) * 0.55f;
                    length = 5.0;
                    break;
                case Ride:
                    out = filterA.processSingleSampleRaw (metal() + white() * 0.25f) * (env (t, 0.6) * 0.5f + env (t, 0.02) * 0.4f)
                        + tone (2900.0) * env (t, 0.5) * 0.05f;
                    length = 3.0;
                    break;
                case Perc:
                {
                    const float attack = (float) juce::jmin (1.0, t / 0.008);
                    out = filterA.processSingleSampleRaw (white()) * attack * env (t, 0.045) * 1.2f;
                    length = 0.3;
                    break;
                }
                case Bodhran:
                    if (! alt)  // open: deep frame-drum boom
                        out = drum (110.0, 68.0, 0.04) * env (t, 0.25) * 0.95f + filterA.processSingleSampleRaw (white()) * env (t, 0.03) * 0.5f;
                    else        // top: tighter, higher stick stroke
                        out = drum (190.0, 150.0, 0.02) * env (t, 0.09) * 0.55f + filterA.processSingleSampleRaw (white()) * env (t, 0.02) * 0.5f;
                    length = 1.2;
                    break;
                case Djembe:
                    if (! alt)  // tone
                        out = (float) (2.0 / juce::MathConstants<double>::pi * std::asin (drum (340.0, 290.0, 0.015))) * env (t, 0.15) * 0.7f
                            + filterA.processSingleSampleRaw (white()) * env (t, 0.04) * 0.35f;
                    else        // slap
                        out = filterA.processSingleSampleRaw (white()) * env (t, 0.06) * 1.1f + tone (440.0) * env (t, 0.04) * 0.35f;
                    length = 0.8;
                    break;
                case Cajon:
                    if (! alt)  // bass
                        out = drum (120.0, 62.0, 0.025) * env (t, 0.22) * 0.95f + filterA.processSingleSampleRaw (white()) * env (t, 0.02) * 0.4f;
                    else        // slap with snare wires
                        out = filterA.processSingleSampleRaw (white()) * env (t, 0.15) * 0.75f + tone (240.0) * env (t, 0.05) * 0.4f;
                    length = 0.9;
                    break;
                case Darbuka:
                    if (! alt)  // doum: ringing low stroke
                        out = drum (150.0, 105.0, 0.05) * env (t, 0.45) * 0.9f;
                    else        // tek: sharp rim click
                        out = filterA.processSingleSampleRaw (white()) * env (t, 0.025) + tone (1250.0) * env (t, 0.02) * 0.25f;
                    length = 2.0;
                    break;
                case Dundun:
                    if (! alt)  // drum
                        out = drum (82.0, 56.0, 0.035) * env (t, 0.35) + filterA.processSingleSampleRaw (white()) * env (t, 0.015) * 0.3f;
                    else        // bell
                        out = filterA.processSingleSampleRaw (bell()) * env (t, 0.18) * 0.6f;
                    length = 1.6;
                    break;
                default: break;
            }

            if (choking)
            {
                chokeGain *= 0.995f;
                if (chokeGain < 0.001f) active = false;
            }

            t += 1.0 / sr;
            if (t > length) active = false;

            const float velCurve = 0.15f + 0.85f * vel * vel;
            return out * velCurve * chokeGain * 0.5f;
        }
    };

    double sampleRate = 44100.0;
    std::array<Voice, NumLanes * 2> voices;
};

} // namespace gf

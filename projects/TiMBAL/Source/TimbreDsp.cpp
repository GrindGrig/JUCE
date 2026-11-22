#include "TimbreDsp.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

namespace tt
{
namespace
{
    constexpr double twoPi = 6.283185307179586;

    inline float midiToHz (float m) noexcept { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }
    inline float hzToMidi (float f) noexcept { return 69.0f + 12.0f * std::log2 (f / 440.0f); }

    void normaliseMax (Envelope& e) noexcept
    {
        const float m = *std::max_element (e.begin(), e.end());
        for (auto& v : e) v -= m;
    }

    std::vector<double> periodicHann (int n)
    {
        std::vector<double> w ((size_t) n);
        for (int i = 0; i < n; ++i)
            w[(size_t) i] = 0.5 - 0.5 * std::cos (twoPi * i / n);
        return w;
    }
}

//==============================================================================
float gridFrequency (int g) noexcept
{
    return gridLowHz * std::exp (std::log (gridHighHz / gridLowHz) * (float) g / (float) (gridSize - 1));
}

float envelopeAt (const Envelope& env, float hz) noexcept
{
    const float pos = juce::jlimit (0.0f, (float) (gridSize - 1),
                                    std::log (juce::jmax (hz, gridLowHz) / gridLowHz) / std::log (gridHighHz / gridLowHz) * (float) (gridSize - 1));
    const int i = (int) pos;
    if (i >= gridSize - 1) return env[gridSize - 1];
    const float fr = pos - (float) i;
    return env[(size_t) i] + (env[(size_t) i + 1] - env[(size_t) i]) * fr;
}

//==============================================================================
Fft::Fft (int order) : n (1 << order), cosT ((size_t) n / 2), sinT ((size_t) n / 2), rev ((size_t) n)
{
    for (int i = 0; i < n / 2; ++i)
    {
        cosT[(size_t) i] = std::cos (twoPi * i / n);
        sinT[(size_t) i] = std::sin (twoPi * i / n);
    }

    for (int i = 0; i < n; ++i)
    {
        int r = 0, x = i;
        for (int b = 0; b < order; ++b) { r = (r << 1) | (x & 1); x >>= 1; }
        rev[(size_t) i] = r;
    }
}

void Fft::perform (double* re, double* im, bool inverse) const noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const int j = rev[(size_t) i];
        if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }

    for (int size = 2; size <= n; size <<= 1)
    {
        const int half = size >> 1, step = n / size;

        for (int i = 0; i < n; i += size)
            for (int j = 0, k = 0; j < half; ++j, k += step)
            {
                const int a = i + j, b = a + half;
                const double c = cosT[(size_t) k], s = inverse ? sinT[(size_t) k] : -sinT[(size_t) k];
                const double tr = re[b] * c - im[b] * s, ti = re[b] * s + im[b] * c;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
            }
    }
}

int fftOrderFor (double sampleRate) noexcept
{
    return juce::jlimit (9, 13, (int) std::lround (std::log2 (sampleRate * 0.046)));
}

//==============================================================================
void PitchDetector::prepare (double sampleRate)
{
    decimation = juce::jmax (1, (int) std::lround (sampleRate / 11025.0));
    decimatedRate = sampleRate / decimation;
    window = (int) std::lround (decimatedRate * 0.027);
    tauMax = (int) (decimatedRate / 48.0);
    tauMin = juce::jmax (2, (int) (decimatedRate / 1100.0));
    diff.assign ((size_t) tauMax + 2, 0.0);
    cmnd.assign ((size_t) tauMax + 2, 0.0);
}

float PitchDetector::detect (const float* end, float& confidence) const noexcept
{
    const float* d = end - span();
    confidence = 0.0f;

    double energy = 0.0;
    for (int j = 0; j < window; ++j) energy += (double) d[j] * d[j];
    if (energy / window < 3.0e-9)
        return 0.0f;

    for (int t = 1; t <= tauMax + 1; ++t)
    {
        double sum = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const double q = (double) d[j] - d[j + t];
            sum += q * q;
        }
        diff[(size_t) t] = sum;
    }

    cmnd[0] = 1.0;
    double running = 0.0;
    for (int t = 1; t <= tauMax + 1; ++t)
    {
        running += diff[(size_t) t];
        cmnd[(size_t) t] = running > 0.0 ? diff[(size_t) t] * t / running : 1.0;
    }

    int tau = -1;
    for (int t = tauMin; t <= tauMax; ++t)
        if (cmnd[(size_t) t] < 0.15)
        {
            while (t + 1 <= tauMax && cmnd[(size_t) t + 1] < cmnd[(size_t) t]) ++t;
            tau = t;
            break;
        }

    if (tau < 0)
    {
        double best = 1.0e9;
        for (int t = tauMin; t <= tauMax; ++t)
            if (cmnd[(size_t) t] < best) { best = cmnd[(size_t) t]; tau = t; }
    }

    const double a = cmnd[(size_t) tau - 1], b = cmnd[(size_t) tau], c = cmnd[(size_t) tau + 1];
    const double den = a - 2.0 * b + c;
    const double shift = ! juce::exactlyEqual (den, 0.0) ? juce::jlimit (-1.0, 1.0, 0.5 * (a - c) / den) : 0.0;
    confidence = (float) (1.0 - juce::jmin (1.0, b));
    return (float) (decimatedRate / (tau + shift));
}

//==============================================================================
void FrameAnalyser::prepare (int fftOrder, double rate)
{
    fft = std::make_unique<Fft> (fftOrder);
    size = fft->size();
    sampleRate = rate;
    window = periodicHann (size);
    re.assign ((size_t) size, 0.0);
    im.assign ((size_t) size, 0.0);
}

float FrameAnalyser::analyse (const float* frame, float* mags, float& centroidHz) noexcept
{
    double energy = 0.0;
    for (int j = 0; j < size; ++j)
    {
        const double v = frame[j];
        energy += v * v;
        re[(size_t) j] = v * window[(size_t) j];
        im[(size_t) j] = 0.0;
    }

    fft->perform (re.data(), im.data(), false);

    double sm = 0.0, sf = 0.0;
    for (int b = 0; b <= size / 2; ++b)
    {
        const double a = std::sqrt (re[(size_t) b] * re[(size_t) b] + im[(size_t) b] * im[(size_t) b]);
        mags[b] = (float) a;
        sm += a;
        sf += a * b;
    }

    centroidHz = sm > 0.0 ? (float) (sf / sm * sampleRate / size) : 0.0f;
    return (float) std::sqrt (energy / size);
}

void bandLevels (const float* mags, int numBins, int fftSize, double sampleRate, Envelope& out) noexcept
{
    for (int g = 0; g < gridSize; ++g)
    {
        const double f = gridFrequency (g), lo = f * 0.94 * fftSize / sampleRate, hi = f * 1.06 * fftSize / sampleRate;
        double v;

        if (hi - lo < 1.2)
        {
            const double b = f * fftSize / sampleRate;
            const int i = (int) b;
            const double fr = b - i;
            const double m0 = i < numBins ? mags[i] : 0.0, m1 = i + 1 < numBins ? mags[i + 1] : 0.0;
            v = m0 * (1.0 - fr) + m1 * fr;
            v *= v;
        }
        else
        {
            double s = 0.0;
            int c = 0;
            for (int b = (int) std::ceil (lo); b <= (int) std::floor (hi) && b < numBins; ++b) { s += (double) mags[b] * mags[b]; ++c; }
            v = c > 0 ? s / c : 0.0;
        }

        out[(size_t) g] = (float) (10.0 * std::log10 (v + 1.0e-14));
    }
}

//==============================================================================
std::unique_ptr<TimbreModel> analyseTimbre (std::vector<float> x, double sampleRate, const juce::String& name)
{
    auto model = std::make_unique<TimbreModel>();
    auto& M = *model;
    M.name = name;
    M.sampleRate = sampleRate;

    const int order = fftOrderFor (sampleRate);
    M.fftSize = 1 << order;
    M.hop = M.fftSize / 4;
    M.numBins = M.fftSize / 2 + 1;
    const int N = M.fftSize, H = M.hop, NB = M.numBins;
    const int len = (int) x.size();
    const int nF = len / H + 2;

    FrameAnalyser fa;
    fa.prepare (order, sampleRate);

    M.loudness.resize ((size_t) nF); M.rms.resize ((size_t) nF); M.centroid.resize ((size_t) nF);
    M.mags.resize ((size_t) nF * (size_t) NB);
    std::vector<float> frame ((size_t) N);

    for (int i = 0; i < nF; ++i)
    {
        const int off = i * H - N / 2;
        for (int j = 0; j < N; ++j)
        {
            const int k = off + j;
            frame[(size_t) j] = (k >= 0 && k < len) ? x[(size_t) k] : 0.0f;
        }

        float cent = 0.0f;
        const float r = fa.analyse (frame.data(), M.mags.data() + (size_t) i * (size_t) NB, cent);
        M.rms[(size_t) i] = r;
        M.loudness[(size_t) i] = 20.0f * std::log10 (r + 1.0e-9f);
        M.centroid[(size_t) i] = cent;
    }

    const float maxL = *std::max_element (M.loudness.begin(), M.loudness.end());
    M.gateDb = juce::jmax (maxL - 48.0f, -78.0f);

    // pitch on a decimated copy; each frame's window ends where the STFT frame ends (as in the live engine)
    PitchDetector pd;
    pd.prepare (sampleRate);
    const int D = pd.decimation, pad = pd.span();
    std::vector<float> dec ((size_t) (len / D + 2 * pad + N / D + 4), 0.0f);
    for (int k = 0; k < len / D; ++k)
    {
        float s = 0.0f;
        for (int q = 0; q < D; ++q) s += x[(size_t) (k * D + q)];
        dec[(size_t) (pad + k)] = s / (float) D;
    }

    std::vector<float> f0 ((size_t) nF, 0.0f), conf ((size_t) nF, 0.0f);
    for (int i = 0; i < nF; ++i)
    {
        const int endIdx = juce::jlimit (0, (int) dec.size() - pad - 1, (i * H + N / 2) / D);
        f0[(size_t) i] = pd.detect (dec.data() + pad + endIdx, conf[(size_t) i]);
    }

    M.voiced.assign ((size_t) nF, 0);
    for (int i = 0; i < nF; ++i)
        M.voiced[(size_t) i] = (conf[(size_t) i] > 0.8f && M.loudness[(size_t) i] > M.gateDb + 4.0f
                                && f0[(size_t) i] > 45.0f && f0[(size_t) i] < 1100.0f) ? 1 : 0;

    for (int i = 0; i < nF;)
    {
        if (! M.voiced[(size_t) i]) { ++i; continue; }
        int j = i;
        while (j < nF && M.voiced[(size_t) j]) ++j;
        if (j - i < 3) for (int k = i; k < j; ++k) M.voiced[(size_t) k] = 0;
        i = j;
    }

    M.midi.assign ((size_t) nF, 0.0f);
    std::vector<float> raw ((size_t) nF, 0.0f);
    for (int i = 0; i < nF; ++i)
        if (M.voiced[(size_t) i]) raw[(size_t) i] = hzToMidi (f0[(size_t) i]);

    for (int i = 0; i < nF; ++i)
        if (M.voiced[(size_t) i])
        {
            float w[5]; int c = 0;
            for (int k = i - 2; k <= i + 2; ++k)
                if (k >= 0 && k < nF && M.voiced[(size_t) k]) w[c++] = raw[(size_t) k];
            std::sort (w, w + c);
            M.midi[(size_t) i] = w[c / 2];
            f0[(size_t) i] = midiToHz (M.midi[(size_t) i]);
        }

    // loudness percentile within the clip
    std::vector<float> act;
    for (int i = 0; i < nF; ++i) if (M.loudness[(size_t) i] > M.gateDb) act.push_back (M.loudness[(size_t) i]);
    std::sort (act.begin(), act.end());
    const auto pct = [&act] (float L)
    {
        if (act.empty()) return 0.5f;
        return (float) (std::lower_bound (act.begin(), act.end(), L) - act.begin()) / (float) act.size();
    };

    M.loudPct.resize ((size_t) nF); M.logCentroid.resize ((size_t) nF);
    for (int i = 0; i < nF; ++i)
    {
        M.loudPct[(size_t) i] = pct (M.loudness[(size_t) i]);
        M.logCentroid[(size_t) i] = std::log2 (M.centroid[(size_t) i] + 60.0f);
        if (M.loudness[(size_t) i] > M.gateDb + 3.0f) M.active.push_back (i);
    }

    std::vector<float> vm;
    for (int i = 0; i < nF; ++i) if (M.voiced[(size_t) i]) vm.push_back (M.midi[(size_t) i]);
    std::sort (vm.begin(), vm.end());
    M.hasPitch = ! vm.empty();
    if (M.hasPitch)
    {
        const auto q = [&vm] (float p) { return vm[(size_t) (p * (float) (vm.size() - 1))]; };
        M.medianMidi = q (0.5f); M.lowMidi = q (0.1f); M.highMidi = q (0.9f);
    }

    // timbre envelopes per loudness tier
    struct Acc { std::array<double, gridSize> h {}, n {}; double nf = 0.0; int c = 0; };
    std::array<Acc, 3> tierAcc;
    Acc all, uv;
    std::vector<float> minFiltered ((size_t) NB);
    std::vector<double> hd (202, 0.0);
    Envelope nd {}, env {};

    for (int i = 0; i < nF; ++i)
    {
        const float L = M.loudness[(size_t) i];
        if (L < M.gateDb + 6.0f) continue;
        const float* m = M.frameMags (i);

        if (M.voiced[(size_t) i] && conf[(size_t) i] > 0.85f)
        {
            const double fz = f0[(size_t) i];
            const int K = juce::jmin (200, (int) (juce::jmin ((double) gridHighHz, sampleRate * 0.47) / fz));
            if (K < 1) continue;

            for (int k = 1; k <= K; ++k)
            {
                const int bi = (int) std::lround (k * fz * N / sampleRate);
                float pk = 0.0f;
                for (int b = bi - 1; b <= bi + 1; ++b) if (b >= 0 && b < NB) pk = juce::jmax (pk, m[b]);
                const double amp = pk * 4.0 / N;
                hd[(size_t) k] = 20.0 * std::log10 (amp + 1.0e-9) - L;
            }

            for (int g = 0; g < gridSize; ++g)
            {
                const double f = gridFrequency (g);
                if (f <= fz) env[(size_t) g] = (float) hd[1];
                else
                {
                    const double kf = f / fz;
                    const int k0 = (int) kf;
                    env[(size_t) g] = (float) (k0 >= K ? hd[(size_t) K] - 12.0 * std::log2 (f / (K * fz))
                                                       : hd[(size_t) k0] + (hd[(size_t) k0 + 1] - hd[(size_t) k0]) * (kf - k0));
                }
            }

            // noise fraction: energy near harmonics vs. the rest, minus the noise under the harmonics
            const double sp = fz * N / sampleRate;
            const double rad = juce::jmax (1.0, juce::jmin (2.0, std::floor (sp / 2.0 - 0.5)));
            double eH = 0.0, eR = 0.0; int cH = 0, cR = 0;
            for (int b = 1; b < NB - 1 && b * sampleRate / N < gridHighHz; ++b)
            {
                const double kf = b / sp;
                const double nearest = std::round (kf);
                const double e2 = (double) m[b] * m[b];
                if (nearest >= 1.0 && std::abs (b - nearest * sp) <= rad) { eH += e2; ++cH; } else { eR += e2; ++cR; }
            }
            const double dens = cR > 0 ? eR / cR : 0.0;
            const double harm = juce::jmax (0.0, eH - dens * cH);
            const double nf = juce::jlimit (0.01, 0.95, 1.0 - harm / (eH + eR + 1.0e-20));

            const int w = juce::jmax (2, (int) std::lround (sp / 2.0));
            for (int b = 0; b < NB; ++b)
            {
                float mn = 1.0e9f;
                for (int k = juce::jmax (0, b - w); k <= juce::jmin (NB - 1, b + w); ++k) mn = juce::jmin (mn, m[k]);
                minFiltered[(size_t) b] = mn;
            }
            bandLevels (minFiltered.data(), NB, N, sampleRate, nd);

            const float p = M.loudPct[(size_t) i];
            for (auto* a : { &tierAcc[p < 1.0f / 3.0f ? 0 : (p < 2.0f / 3.0f ? 1 : 2)], &all })
            {
                for (int g = 0; g < gridSize; ++g) { a->h[(size_t) g] += env[(size_t) g]; a->n[(size_t) g] += nd[(size_t) g] - L; }
                a->nf += nf; a->c++;
            }
        }
        else if (! M.voiced[(size_t) i])
        {
            bandLevels (m, NB, N, sampleRate, nd);
            for (int g = 0; g < gridSize; ++g) uv.n[(size_t) g] += nd[(size_t) g] - L;
            uv.c++;
        }
    }

    M.pitched = all.c > 0;

    if (! M.pitched)
    {
        // no steady pitch: use the clip's average spectrum for every envelope
        std::array<double, gridSize> avg {};
        int c = 0;
        for (int i = 0; i < nF; ++i)
            if (M.loudness[(size_t) i] > M.gateDb + 6.0f)
            {
                bandLevels (M.frameMags (i), NB, N, sampleRate, nd);
                for (int g = 0; g < gridSize; ++g) avg[(size_t) g] += nd[(size_t) g] - M.loudness[(size_t) i];
                ++c;
            }
        for (auto& t : M.tiers)
        {
            for (int g = 0; g < gridSize; ++g) t.harmonic[(size_t) g] = t.noise[(size_t) g] = (float) (avg[(size_t) g] / juce::jmax (1, c));
            t.noiseFraction = 0.5f;
        }
    }
    else
    {
        for (size_t t = 0; t < 3; ++t)
        {
            const Acc& a = tierAcc[t].c > 0 ? tierAcc[t] : all;
            for (int g = 0; g < gridSize; ++g)
            {
                M.tiers[t].harmonic[(size_t) g] = (float) (a.h[(size_t) g] / a.c);
                M.tiers[t].noise[(size_t) g] = (float) (a.n[(size_t) g] / a.c);
            }
            M.tiers[t].noiseFraction = (float) (a.nf / a.c);
        }
    }

    if (uv.c > 0) for (int g = 0; g < gridSize; ++g) M.unvoicedNoise[(size_t) g] = (float) (uv.n[(size_t) g] / uv.c);
    else          M.unvoicedNoise = M.tiers[1].noise;

    // the loud tier's peak sits at 0 dB; tiers keep their relative levels
    float ref = -1.0e9f;
    for (auto& t : M.tiers) ref = juce::jmax (ref, *std::max_element (t.harmonic.begin(), t.harmonic.end()));
    for (auto& t : M.tiers) { for (auto& v : t.harmonic) v -= ref; normaliseMax (t.noise); }
    normaliseMax (M.unvoicedNoise);

    M.audio = std::move (x);
    return model;
}

//==============================================================================
// Built-in timbres: synthesised stand-ins so the plug-in works before a file is loaded.
namespace
{
    struct Rng
    {
        juce::uint32 s;
        float next() noexcept { s = s * 1664525u + 1013904223u; return (float) s / 4294967296.0f; }
    };

    std::vector<float> whiteNoise (size_t len, juce::uint32 seed)
    {
        Rng r { seed };
        std::vector<float> a (len);
        for (auto& v : a) v = r.next() * 2.0f - 1.0f;
        return a;
    }

    enum class Bq { bandpass, highpass };

    std::vector<float> biquad (const std::vector<float>& x, Bq type, double f, double q, double sr)
    {
        const double w = twoPi * f / sr, al = std::sin (w) / (2.0 * q), cw = std::cos (w);
        double b0, b1, b2;
        const double a0 = 1.0 + al, a1 = -2.0 * cw, a2 = 1.0 - al;
        if (type == Bq::bandpass) { b0 = al; b1 = 0.0; b2 = -al; }
        else                      { b0 = (1.0 + cw) / 2.0; b1 = -(1.0 + cw); b2 = (1.0 + cw) / 2.0; }

        std::vector<float> y (x.size());
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double v = (b0 * x[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1; x1 = x[i]; y2 = y1; y1 = v;
            y[i] = (float) v;
        }
        return y;
    }

    struct Note { float midi; float dur; float vel; };   // midi < 0 means a rest

    struct Sequence
    {
        size_t len = 0;
        std::vector<float> f0, env, dyn, age;
    };

    struct SeqOptions { float attack, release, glide, vibrato = 0.0f, vibratoRate = 5.5f; bool swell = false; float jitter = 0.02f; juce::uint32 seed = 7; };

    Sequence makeSequence (const std::vector<Note>& notes, const SeqOptions& o, double sr)
    {
        float total = 0.3f;
        for (auto& n : notes) total += n.dur;

        Sequence s;
        s.len = (size_t) (total * sr);
        s.f0.assign (s.len, 220.0f); s.env.assign (s.len, 0.0f); s.dyn.assign (s.len, 0.0f); s.age.assign (s.len, 0.0f);

        const double gk = 1.0 - std::exp (-1.0 / (o.glide * sr));
        Rng r { o.seed };
        double t = 0.0, jit = 0.0;
        float cur = -1.0f, lastEnv = 0.0f;

        for (auto& n : notes)
        {
            const size_t s0 = (size_t) std::lround (t * sr), s1 = juce::jmin (s.len, (size_t) std::lround ((t + n.dur) * sr));
            for (size_t i = s0; i < s1; ++i)
            {
                const float a = (float) ((double) (i - s0) / sr);
                if (n.midi < 0.0f) { s.f0[i] = cur > 0.0f ? midiToHz (cur) : 220.0f; lastEnv = 0.0f; continue; }

                cur = (cur < 0.0f || (juce::exactlyEqual (lastEnv, 0.0f) && i == s0)) ? n.midi : cur + (n.midi - cur) * (float) gk;
                if (i % 64 == 0) jit += (r.next() - 0.5) * 0.6 - jit * 0.05;
                const float vib = o.vibrato * juce::jmin (1.0f, a / 0.3f) * (float) std::sin (twoPi * o.vibratoRate * (double) i / sr) / 100.0f;
                s.f0[i] = midiToHz (cur + vib + (float) jit * o.jitter);
                const float e = juce::jmax (0.0f, juce::jmin (1.0f, a / o.attack) * juce::jmin (1.0f, (n.dur - a) / o.release));
                s.env[i] = e; lastEnv = e;
                s.dyn[i] = juce::jlimit (0.0f, 1.0f, o.swell ? n.vel * (0.3f + 0.7f * std::pow (std::sin (juce::MathConstants<float>::pi * a / n.dur), 0.8f)) : n.vel);
                s.age[i] = a;
            }
            t += n.dur;
        }

        for (size_t i = (size_t) std::lround (t * sr); i < s.len; ++i)
            s.f0[i] = cur > 0.0f ? midiToHz (cur) : 220.0f;
        return s;
    }

    template <typename AmpFn>
    std::vector<float> additive (const Sequence& sq, int K, AmpFn&& fn, double sr, float detuneCents = 0.0f)
    {
        std::vector<float> out (sq.len), A0 ((size_t) K), A1 ((size_t) K);
        const double mul = std::pow (2.0, detuneCents / 1200.0);
        const size_t B = 64;
        fn ((size_t) 0, A0.data());
        double ph = 0.0;

        for (size_t b0 = 0; b0 < sq.len; b0 += B)
        {
            fn (juce::jmin (sq.len - 1, b0 + B), A1.data());
            const size_t bl = juce::jmin (B, sq.len - b0);
            for (size_t j = 0; j < bl; ++j)
            {
                const size_t n = b0 + j;
                const float fr = (float) j / (float) B;
                ph += twoPi * sq.f0[n] * mul / sr;
                if (ph > twoPi) ph -= twoPi;
                const double c2 = 2.0 * std::cos (ph);
                double s0 = 0.0, s1 = std::sin (ph), acc = 0.0;
                for (int k = 0; k < K; ++k)
                {
                    acc += (A0[(size_t) k] + (A1[(size_t) k] - A0[(size_t) k]) * fr) * s1;
                    const double s2 = c2 * s1 - s0; s0 = s1; s1 = s2;
                }
                out[n] = (float) acc;
            }
            std::swap (A0, A1);
        }
        return out;
    }

    std::vector<float> mixNormalised (std::initializer_list<std::pair<const std::vector<float>*, float>> parts)
    {
        const size_t len = parts.begin()->first->size();
        std::vector<float> out (len, 0.0f);
        for (auto& p : parts)
            for (size_t i = 0; i < len && i < p.first->size(); ++i) out[i] += (*p.first)[i] * p.second;
        float pk = 0.0f;
        for (auto v : out) pk = juce::jmax (pk, std::abs (v));
        if (pk > 0.0f) for (auto& v : out) v *= 0.85f / pk;
        return out;
    }

    std::vector<float> shapedNoise (const Sequence& sq, const std::vector<float>& nz, std::function<float (size_t)> fn)
    {
        std::vector<float> o (sq.len);
        for (size_t i = 0; i < sq.len; ++i) o[i] = nz[i] * fn (i);
        return o;
    }

    inline float res (float f, float F, float B) noexcept { const float q = (f - F) / (B * 0.5f); return 1.0f / (1.0f + q * q); }

    std::vector<float> makeCello (double sr)
    {
        const std::vector<Note> notes { { 36, 1.4f, .9f }, { 43, 1.1f, .6f }, { 48, 1.2f, .8f }, { 52, 1.0f, .5f }, { 55, 1.3f, 1.0f },
                                        { 57, .9f, .55f }, { 60, 1.2f, .85f }, { 64, 1.1f, .6f }, { 67, 1.4f, 1.0f }, { 62, 1.0f, .45f },
                                        { 55, 1.1f, .7f }, { 50, 1.0f, .5f }, { 45, 1.5f, .9f } };
        const auto sq = makeSequence (notes, { .09f, .14f, .03f, .14f, 5.6f, true, .03f, 12 }, sr);
        auto h = additive (sq, 56, [&] (size_t n, float* A)
        {
            const float f = sq.f0[n], e = sq.env[n], d = sq.dyn[n];
            for (int k = 1; k <= 56; ++k)
            {
                const float fk = (float) k * f;
                if (fk > sr * 0.46 || juce::exactlyEqual (e, 0.0f)) { A[k - 1] = 0.0f; continue; }
                const float body = 0.12f + 0.9f * res (fk, 205, 90) + 0.75f * res (fk, 290, 80) + 0.8f * res (fk, 440, 150) + 0.6f * res (fk, 590, 180)
                                 + 0.5f * res (fk, 1100, 420) + 0.45f * res (fk, 2350, 800) + 0.25f * res (fk, 3400, 900);
                A[k - 1] = e * (0.2f + 0.8f * d) / (float) k * body * std::exp (-fk / (800.0f + 4300.0f * d));
            }
        }, sr);
        const auto nz = whiteNoise (sq.len, 31);
        const auto bow = shapedNoise (sq, biquad (nz, Bq::bandpass, 750, .9, sr), [&] (size_t i) { return sq.env[i] * (0.15f + 0.85f * sq.dyn[i]); });
        const auto bow2 = shapedNoise (sq, biquad (nz, Bq::bandpass, 2500, 1.1, sr), [&] (size_t i) { return sq.env[i] * sq.dyn[i]; });
        return mixNormalised ({ { &h, 1.0f }, { &bow, 0.05f }, { &bow2, 0.035f } });
    }

    std::vector<float> makeSynth (double sr)
    {
        const std::vector<Note> notes { { 45, .55f, .7f }, { 52, .55f, .5f }, { 57, .55f, .8f }, { 60, .55f, .6f }, { -1, .1f, 0 }, { 64, .8f, 1.0f },
                                        { 57, .45f, .5f }, { 48, .55f, .75f }, { 55, .55f, .5f }, { 60, .55f, .7f }, { 67, 1.0f, 1.0f }, { -1, .1f, 0 },
                                        { 62, .5f, .6f }, { 57, .5f, .45f }, { 53, .5f, .7f }, { 50, 1.1f, .85f } };
        const auto sq = makeSequence (notes, { .008f, .14f, .05f, 0.0f, 5.5f, false, .02f, 15 }, sr);
        const auto lp2 = [] (float f, float fc, float q) { const float r = f / fc; return 1.0f / std::sqrt ((1.0f - r * r) * (1.0f - r * r) + (r / q) * (r / q)); };
        const auto fn = [&] (size_t n, float* A)
        {
            const float f = sq.f0[n], e = sq.env[n], d = sq.dyn[n], fe = std::exp (-sq.age[n] / .22f);
            const float fc = 280.0f + 900.0f * d + 2600.0f * fe * d;
            for (int k = 1; k <= 60; ++k)
            {
                const float fk = (float) k * f;
                A[k - 1] = (fk > sr * 0.46 || juce::exactlyEqual (e, 0.0f)) ? 0.0f : e * (0.4f + 0.6f * d) / (float) k * lp2 (fk, fc, 2.6f) * lp2 (fk, fc, 0.7f);
            }
        };
        const auto a = additive (sq, 60, fn, sr, -8.0f), b = additive (sq, 60, fn, sr, 8.0f);
        return mixNormalised ({ { &a, 0.5f }, { &b, 0.5f } });
    }

    std::vector<float> makeFlute (double sr)
    {
        const std::vector<Note> notes { { 72, .5f, .5f }, { 74, .5f, .6f }, { 76, .5f, .7f }, { 79, .9f, .9f }, { 77, .4f, .6f }, { 76, .4f, .55f },
                                        { 74, .6f, .5f }, { -1, .15f, 0 }, { 76, .45f, .6f }, { 79, .45f, .75f }, { 81, .45f, .85f }, { 84, 1.0f, 1.0f },
                                        { 83, .45f, .7f }, { 79, .45f, .6f }, { 76, 1.1f, .5f } };
        const auto sq = makeSequence (notes, { .07f, .09f, .03f, .22f, 5.1f, true, .02f, 18 }, sr);
        const auto h = additive (sq, 12, [&] (size_t n, float* A)
        {
            const float f = sq.f0[n], e = sq.env[n], d = sq.dyn[n];
            for (int k = 1; k <= 12; ++k)
            {
                const float fk = (float) k * f;
                A[k - 1] = (fk > sr * 0.46 || juce::exactlyEqual (e, 0.0f)) ? 0.0f : e * (0.35f + 0.65f * d) * std::exp (-(float) (k - 1) * (1.7f - 0.9f * d));
            }
        }, sr);
        const auto br = shapedNoise (sq, biquad (whiteNoise (sq.len, 41), Bq::bandpass, 2600, .55, sr), [&] (size_t i) { return sq.env[i] * (0.5f + 0.5f * sq.dyn[i]); });
        return mixNormalised ({ { &h, 1.0f }, { &br, 0.11f } });
    }

    std::vector<float> makeBrass (double sr)
    {
        const std::vector<Note> notes { { 55, .5f, .4f }, { 58, .5f, .55f }, { 60, .5f, .7f }, { 63, .8f, 1.0f }, { -1, .12f, 0 }, { 65, .35f, .5f },
                                        { 67, .35f, .65f }, { 70, .9f, 1.0f }, { 67, .4f, .45f }, { 63, .4f, .6f }, { 60, 1.1f, .8f }, { -1, .12f, 0 },
                                        { 58, .45f, .3f }, { 60, .45f, .5f }, { 55, 1.2f, .9f } };
        const auto sq = makeSequence (notes, { .045f, .08f, .025f, .12f, 5.3f, false, .02f, 22 }, sr);
        const auto h = additive (sq, 50, [&] (size_t n, float* A)
        {
            const float f = sq.f0[n], e = sq.env[n], d = sq.dyn[n], br = d * juce::jmin (1.0f, sq.age[n] / .07f);
            for (int k = 1; k <= 50; ++k)
            {
                const float fk = (float) k * f;
                A[k - 1] = (fk > sr * 0.46 || juce::exactlyEqual (e, 0.0f)) ? 0.0f
                         : e * (0.25f + 0.75f * d) * std::pow ((float) k, -0.3f) * std::exp (-fk / (350.0f + 4300.0f * std::pow (br, 1.4f))) * (1.0f + 1.2f * res (fk, 1250, 700));
            }
        }, sr);
        return mixNormalised ({ { &h, 1.0f } });
    }
}

juce::StringArray builtInTimbreNames() { return { "Cello", "Analog synth", "Flute", "Brass" }; }

std::vector<float> synthesiseBuiltIn (int index, double sr)
{
    switch (index)
    {
        case 1:  return makeSynth (sr);
        case 2:  return makeFlute (sr);
        case 3:  return makeBrass (sr);
        default: return makeCello (sr);
    }
}

//==============================================================================
TransferEngine::TransferEngine() { prepare (44100.0); }

void TransferEngine::prepare (double rate)
{
    sampleRate = rate;
    fftOrder = fftOrderFor (rate);
    fftSize = 1 << fftOrder;
    hop = fftSize / 4;
    numBins = fftSize / 2 + 1;
    ringSize = fftSize * 4;
    ringMask = ringSize - 1;

    analyser.prepare (fftOrder, rate);
    pitch.prepare (rate);
    fft = std::make_unique<Fft> (fftOrder);

    decRingSize = (int) juce::nextPowerOfTwo (pitch.span() * 2);
    decMask = decRingSize - 1;

    inRing.assign ((size_t) ringSize, 0.0f);
    acc.assign ((size_t) ringSize, 0.0f);
    decRing.assign ((size_t) decRingSize, 0.0f);
    hann = periodicHann (fftSize);
    re.assign ((size_t) fftSize, 0.0); im.assign ((size_t) fftSize, 0.0);
    re2.assign ((size_t) fftSize, 0.0); im2.assign ((size_t) fftSize, 0.0);
    envA.assign ((size_t) numBins, 0.0); envB.assign ((size_t) numBins, 0.0);
    frame.assign ((size_t) fftSize, 0.0f);
    mags.assign ((size_t) numBins, 0.0f);
    decWindow.assign ((size_t) pitch.span(), 0.0f);

    const auto makeWindow = [] (int n) { std::vector<float> w ((size_t) n); for (int i = 0; i < n; ++i) w[(size_t) i] = 0.5f - 0.5f * (float) std::cos (twoPi * i / n); return w; };
    grainShort = makeWindow (fftSize / 2);
    grainLong = makeWindow (fftSize);

    bandList.reserve (80);
    bandValues.assign (80, 0.0);
    bandCount = -1;
    reset();
}

void TransferEngine::reset()
{
    std::fill (inRing.begin(), inRing.end(), 0.0f);
    std::fill (acc.begin(), acc.end(), 0.0f);
    std::fill (decRing.begin(), decRing.end(), 0.0f);
    samplesIn = 0; hopCounter = 0; decCounter = 0; decSum = 0.0f; decWritten = 0;
    peakLoud = -60.0f; wasVoiced = false; pitchHistCount = 0; lastMidi = 48.0f; haveAverage = false; currentOctave = 0.0f;
    prevAmps.fill (0.0f); curAmps.fill (0.0f); prevK = curK = 0; voicing = 0.0f; voicedRun = 0; prevAge = curAge = 0.0f;
    phase = vibPhase = 0.0; previousGrain = -1; vocoderStep = 0;
}

void TransferEngine::process (const float* in, float* wet, float* dryDelayed, int n, const Settings& s, const TimbreModel* model) noexcept
{
    const int D = pitch.decimation;
    int scopeWrite = outputScope.written.load (std::memory_order_relaxed);

    for (int i = 0; i < n; ++i)
    {
        const float x = in[i];
        inRing[(size_t) (samplesIn & ringMask)] = x;

        decSum += x;
        if (++decCounter == D)
        {
            decRing[(size_t) (decWritten & decMask)] = decSum / (float) D;
            ++decWritten;
            decSum = 0.0f;
            decCounter = 0;
        }

        ++samplesIn;
        if (++hopCounter == hop)
        {
            hopCounter = 0;
            runHop (s, model);
        }

        const juce::int64 content = samplesIn - 1 - fftSize;
        const auto idx = (size_t) (content & ringMask);
        wet[i] = acc[idx];
        acc[idx] = 0.0f;
        dryDelayed[i] = content >= 0 ? inRing[idx] : 0.0f;

        outputScope.items[(size_t) (scopeWrite % (int) outputScope.items.size())] = wet[i];
        ++scopeWrite;
    }

    outputScope.written.store (scopeWrite, std::memory_order_release);
}

float TransferEngine::octaveShift (const Settings& s, const TimbreModel& M) const noexcept
{
    if (! s.autoOctave || ! M.hasPitch || ! haveAverage)
        return 0.0f;
    return 12.0f * std::round ((M.medianMidi - (srcAverage + s.transpose)) / 12.0f);
}

void TransferEngine::runHop (const Settings& s, const TimbreModel* model) noexcept
{
    const juce::int64 n = samplesIn;
    for (int j = 0; j < fftSize; ++j)
        frame[(size_t) j] = inRing[(size_t) ((n - fftSize + j) & ringMask)];

    hopRms = analyser.analyse (frame.data(), mags.data(), hopCentroid);
    hopLoud = 20.0f * std::log10 (hopRms + 1.0e-9f);

    float conf = 0.0f, f0 = 0.0f;
    if (decWritten >= pitch.span())
    {
        for (int k = 0; k < pitch.span(); ++k)
            decWindow[(size_t) k] = decRing[(size_t) ((decWritten - pitch.span() + k) & decMask)];
        f0 = pitch.detect (decWindow.data() + pitch.span(), conf);
    }

    const float hopSec = (float) hop / (float) sampleRate;
    hopAbove = hopLoud > s.gateDb;
    hopVoiced = hopAbove && f0 > 45.0f && f0 < 1100.0f && conf > (wasVoiced ? 0.7f : 0.8f);

    if (hopVoiced)
    {
        const float m = hzToMidi (f0);
        if (pitchHistCount < 3) pitchHist[pitchHistCount++] = m;
        else { pitchHist[0] = pitchHist[1]; pitchHist[1] = pitchHist[2]; pitchHist[2] = m; }

        if (pitchHistCount < 3) hopMidi = pitchHist[pitchHistCount - 1];
        else hopMidi = juce::jmax (juce::jmin (pitchHist[0], pitchHist[1]), juce::jmin (juce::jmax (pitchHist[0], pitchHist[1]), pitchHist[2]));   // median of three
    }
    else
    {
        pitchHistCount = 0;
    }

    peakLoud = juce::jmax (hopLoud, peakLoud - 3.0f * hopSec, s.gateDb + 30.0f);
    hopPct = juce::jlimit (0.0f, 1.0f, (hopLoud - (peakLoud - 36.0f)) / 36.0f);

    if (hopVoiced)
    {
        if (! haveAverage) { srcAverage = hopMidi; haveAverage = true; }
        else srcAverage += (hopMidi - srcAverage) * (1.0f - std::exp (-hopSec / 3.0f));
    }

    if (model != nullptr && (hopVoiced && ! wasVoiced))
        currentOctave = octaveShift (s, *model);
    if (! s.autoOctave) currentOctave = 0.0f;
    appliedShift.store ((int) std::lround (s.transpose + currentOctave), std::memory_order_relaxed);

    float playedMidi = 0.0f;

    if (model != nullptr)
    {
        const bool sameGrid = juce::exactlyEqual (model->sampleRate, sampleRate) && model->fftSize == fftSize && ! model->active.empty();

        switch (s.mode)
        {
            case Mode::harmonic: hopHarmonic (s, *model); playedMidi = hopVoiced ? glideMidi : 0.0f; break;
            case Mode::mosaic:   if (sameGrid) hopMosaic (s, *model); break;
            case Mode::vocoder:  if (sameGrid) hopVocoder (s, *model); break;
        }
    }

    telemetry.push ({ hopVoiced ? hopMidi : 0.0f, hopLoud, playedMidi });
    wasVoiced = hopVoiced;
}

void TransferEngine::hopHarmonic (const Settings& s, const TimbreModel& M) noexcept
{
    const juce::int64 n = samplesIn;
    const float hopSec = (float) hop / (float) sampleRate;
    const float a = hopAbove ? hopRms * juce::jlimit (0.0f, 1.0f, (hopLoud - s.gateDb) / 8.0f) : 0.0f;

    if (hopVoiced) lastMidi = hopMidi;
    float m = lastMidi + s.transpose + currentOctave;
    m += (std::round (m) - m) * s.snap;

    if (hopVoiced && wasVoiced)
        glideMidi += (m - glideMidi) * (s.glideMs <= 0.0f ? 1.0f : 1.0f - std::exp (-hopSec * 1000.0f / s.glideMs));
    else
        glideMidi = m;

    voicedRun = hopVoiced ? voicedRun + 1 : 0;
    curAge = juce::jmin (1.0f, (float) voicedRun * hopSec / 0.35f);
    voicing = 0.5f * voicing + 0.5f * (hopVoiced ? 1.0f : 0.0f);
    const float v = voicing;

    const float pos = juce::jlimit (0.0f, 1.0f, 0.5f + (hopPct - 0.5f) * s.brightness) * 2.0f;
    const int ti = juce::jmin (1, (int) pos);
    const float fr = pos - (float) ti;
    const auto& A = M.tiers[(size_t) ti];
    const auto& B = M.tiers[(size_t) ti + 1];
    for (int g = 0; g < gridSize; ++g)
    {
        env[(size_t) g] = A.harmonic[(size_t) g] + (B.harmonic[(size_t) g] - A.harmonic[(size_t) g]) * fr;
        noiseShape[(size_t) g] = A.noise[(size_t) g] + (B.noise[(size_t) g] - A.noise[(size_t) g]) * fr;
    }
    const float nf = juce::jlimit (0.0f, 0.92f, (A.noiseFraction + (B.noiseFraction - A.noiseFraction) * fr) * s.noise);

    curHz = midiToHz (glideMidi);
    curK = juce::jlimit (0, maxHarmonics, (int) (juce::jmin ((double) gridHighHz, sampleRate * 0.46) / curHz));
    double power = 0.0;
    for (int k = 0; k < maxHarmonics; ++k)
    {
        if (k < curK)
        {
            const float q = std::pow (10.0f, envelopeAt (env, (float) (k + 1) * curHz) / 20.0f);
            curAmps[(size_t) k] = q;
            power += (double) q * q / 2.0;
        }
        else curAmps[(size_t) k] = 0.0f;
    }
    const float hr = a * std::sqrt (v * (1.0f - nf));
    const float scale = power > 0.0 ? hr / (float) std::sqrt (power) : 0.0f;
    for (int k = 0; k < curK; ++k) curAmps[(size_t) k] *= scale;

    // harmonic part between the previous frame centre and this one
    const juce::int64 start = n - fftSize / 2 - hop;
    const int K = juce::jmax (prevK, curK);
    bool silent = true;
    for (int k = 0; k < K && silent; ++k) silent = juce::exactlyEqual (prevAmps[(size_t) k], 0.0f) && juce::exactlyEqual (curAmps[(size_t) k], 0.0f);
    const double vibInc = twoPi * 5.4 / sampleRate;

    for (int j = 0; j < hop; ++j)
    {
        const float t = (float) j / (float) hop;
        double f = prevHz + (curHz - prevHz) * t;
        vibPhase += vibInc;
        if (vibPhase > twoPi) vibPhase -= twoPi;
        if (s.vibratoCents > 0.0f)
            f *= std::pow (2.0, s.vibratoCents * (prevAge + (curAge - prevAge) * t) * std::sin (vibPhase) / 1200.0);
        phase += twoPi * f / sampleRate;
        if (phase > twoPi) phase -= twoPi;
        if (silent) continue;

        const double c2 = 2.0 * std::cos (phase);
        double s0 = 0.0, s1 = std::sin (phase), sum = 0.0;
        for (int k = 0; k < K; ++k)
        {
            sum += (prevAmps[(size_t) k] + (curAmps[(size_t) k] - prevAmps[(size_t) k]) * t) * s1;
            const double s2 = c2 * s1 - s0; s0 = s1; s1 = s2;
        }
        acc[(size_t) ((start + j) & ringMask)] += (float) sum;
    }

    // noise part: shaped random-phase frame, overlap-added over the whole frame
    const float nr = a * std::sqrt (v * nf + (1.0f - v));
    if (nr > 1.0e-7f)
    {
        const bool useOwn = v < 1.0f && s.consonants > 0.0f;
        if (useOwn) { bandLevels (mags.data(), numBins, fftSize, sampleRate, own); normaliseMax (own); }

        Envelope shape;
        for (int g = 0; g < gridSize; ++g)
        {
            const float u = useOwn ? M.unvoicedNoise[(size_t) g] * (1.0f - s.consonants) + own[(size_t) g] * s.consonants
                                   : M.unvoicedNoise[(size_t) g];
            shape[(size_t) g] = noiseShape[(size_t) g] * v + u * (1.0f - v);
        }

        std::fill (re.begin(), re.end(), 0.0);
        std::fill (im.begin(), im.end(), 0.0);
        double s2 = 0.0;
        for (int b = 1; b < numBins - 1; ++b)
        {
            const double mg = std::pow (10.0, envelopeAt (shape, (float) (b * sampleRate / fftSize)) / 20.0);
            const double ph = random.nextDouble() * twoPi;
            re[(size_t) b] = mg * std::cos (ph);
            im[(size_t) b] = mg * std::sin (ph);
            s2 += mg * mg;
        }
        const double sc = nr * fftSize / std::sqrt (2.0 * s2 + 1.0e-20);
        for (int b = 1; b < numBins - 1; ++b)
        {
            re[(size_t) b] *= sc; im[(size_t) b] *= sc;
            re[(size_t) (fftSize - b)] = re[(size_t) b];
            im[(size_t) (fftSize - b)] = -im[(size_t) b];
        }
        fft->perform (re.data(), im.data(), true);

        const double norm = 1.0 / (fftSize * std::sqrt (1.5));
        for (int j = 0; j < fftSize; ++j)
            acc[(size_t) ((n - fftSize + j) & ringMask)] += (float) (re[(size_t) j] * hann[(size_t) j] * norm);
    }

    prevAmps = curAmps; prevHz = curHz; prevK = curK; prevAge = curAge;
}

void TransferEngine::hopMosaic (const Settings& s, const TimbreModel& M) noexcept
{
    if (! hopAbove) { previousGrain = -1; return; }

    const juce::int64 n = samplesIn;
    const int G = s.grain == 0 ? fftSize / 2 : fftSize;
    const auto& win = s.grain == 0 ? grainShort : grainLong;
    const float shift = s.transpose + currentOctave;
    const float sp = hopPct, sc = std::log2 (hopCentroid + 60.0f), sm = hopMidi + shift;
    const bool sv = hopVoiced;
    const int succ = previousGrain >= 0 ? previousGrain + 1 : -1;

    int best = M.active.front();
    float bestCost = 1.0e9f;
    for (const int j : M.active)
    {
        float c = std::abs (sp - M.loudPct[(size_t) j]) + 0.55f * std::abs (sc - M.logCentroid[(size_t) j]);
        const bool tv = M.voiced[(size_t) j] != 0;
        if (sv && tv) c += 0.35f * juce::jmin (1.0f, std::abs (sm - M.midi[(size_t) j]) / 12.0f);
        else if (sv != tv) c += 0.4f;
        if (succ >= 0 && std::abs (j - succ) <= 1) c -= 0.45f * s.continuity;
        if (c < bestCost) { bestCost = c; best = j; }
    }
    previousGrain = best;

    const double r = (sv && M.voiced[(size_t) best]) ? juce::jlimit (0.5, 2.0, std::pow (2.0, (sm - M.midi[(size_t) best]) * s.pitchFix / 12.0)) : 1.0;
    const float gain = juce::jmin (12.0f, hopRms / (M.rms[(size_t) best] + 1.0e-9f)) * 3.0f * (float) hop / (float) G;   // overlap-add normalisation, plus make-up for windowed grains
    const double c0 = (double) best * M.hop - (G / 2) * r;
    const juce::int64 start = n - fftSize / 2 - G / 2;
    const auto len = (juce::int64) M.audio.size();

    for (int k = 0; k < G; ++k)
    {
        const double pos = c0 + k * r;
        const auto i0 = (juce::int64) std::floor (pos);
        if (i0 < 0 || i0 + 1 >= len) continue;
        const float t = (float) (pos - (double) i0);
        const float x = M.audio[(size_t) i0] + (M.audio[(size_t) i0 + 1] - M.audio[(size_t) i0]) * t;
        acc[(size_t) ((start + k) & ringMask)] += win[(size_t) k] * gain * x;
    }
}

void TransferEngine::updateBands (int count) noexcept
{
    if (count == bandCount) return;
    bandCount = count;
    bandList.clear();

    double prevEdge = 0.0;
    for (int b = 0; b <= count; ++b)
    {
        const double edge = 60.0 * std::pow ((sampleRate / 2.0) / 60.0, (double) b / count) * fftSize / sampleRate;
        const int lo = juce::jmin (numBins - 1, (int) prevEdge);
        const int hi = juce::jmin (numBins, juce::jmax (lo + 1, (int) edge));
        prevEdge = edge;
        if (! bandList.empty() && bandList.back().lo == lo) continue;
        if (bandList.size() < bandList.capacity())
            bandList.push_back ({ lo, hi, (float) (lo + hi - 1) * 0.5f });
    }
}

void TransferEngine::smoothEnvelope (const float* m, double* out) const noexcept
{
    const int nb = (int) bandList.size();
    double* val = bandValues.data();
    for (int k = 0; k < nb; ++k)
    {
        double s = 0.0;
        for (int b = bandList[(size_t) k].lo; b < bandList[(size_t) k].hi; ++b) s += (double) m[b] * m[b];
        val[k] = std::log (std::sqrt (s / (bandList[(size_t) k].hi - bandList[(size_t) k].lo)) + 1.0e-9);
    }

    int k = 0;
    for (int b = 0; b < numBins; ++b)
    {
        while (k < nb - 2 && bandList[(size_t) k + 1].centre < (float) b) ++k;
        const auto& a = bandList[(size_t) k];
        const bool hasNext = k + 1 < nb;
        const auto& c = hasNext ? bandList[(size_t) k + 1] : a;
        const double fr = c.centre > a.centre ? juce::jlimit (0.0, 1.0, ((double) b - a.centre) / (c.centre - a.centre)) : 0.0;
        out[b] = std::exp (val[k] + ((hasNext ? val[k + 1] : val[k]) - val[k]) * fr);
    }
}

void TransferEngine::hopVocoder (const Settings& s, const TimbreModel& M) noexcept
{
    if (! hopAbove) return;

    const juce::int64 n = samplesIn;
    updateBands (s.bands);
    smoothEnvelope (mags.data(), envA.data());

    const int j = M.active[(size_t) (vocoderStep++ % (juce::int64) M.active.size())];
    const juce::int64 off = (juce::int64) j * M.hop - fftSize / 2;
    const auto len = (juce::int64) M.audio.size();
    for (int k = 0; k < fftSize; ++k)
    {
        const juce::int64 q = off + k;
        re2[(size_t) k] = (q >= 0 && q < len ? M.audio[(size_t) q] : 0.0f) * hann[(size_t) k];
        im2[(size_t) k] = 0.0;
    }
    fft->perform (re2.data(), im2.data(), false);

    const float* tm = M.frameMags (j);
    smoothEnvelope (tm, envB.data());
    double lg = 0.0;
    for (int b = 1; b < numBins; ++b) lg += std::log (envB[(size_t) b] + 1.0e-9);
    const double gm = std::exp (lg / (numBins - 1));
    const double gmPart = std::pow (gm + 1.0e-9, 1.0 - s.flatten);

    for (int b = 0; b < numBins; ++b)
    {
        double yr = 0.0, yi = 0.0;
        if (b > 0 && b < numBins - 1)
        {
            const double r = tm[b] / (std::pow (envB[(size_t) b] + 1.0e-9, (double) s.flatten) * gmPart);
            const double cr = tm[b] > 0.0f ? re2[(size_t) b] / tm[b] : 1.0, ci = tm[b] > 0.0f ? im2[(size_t) b] / tm[b] : 0.0;
            const double ph = random.nextDouble() * twoPi;
            yr = envA[(size_t) b] * (r * cr + s.air * std::cos (ph));
            yi = envA[(size_t) b] * (r * ci + s.air * std::sin (ph));
        }
        re[(size_t) b] = yr; im[(size_t) b] = yi;
        if (b > 0 && b < numBins - 1) { re[(size_t) (fftSize - b)] = yr; im[(size_t) (fftSize - b)] = -yi; }
    }
    fft->perform (re.data(), im.data(), true);

    const double norm = 0.5 / (fftSize * 1.5);   // carriers peak above their smoothed envelope; this brings the level back near A's
    for (int k = 0; k < fftSize; ++k)
        acc[(size_t) ((n - fftSize + k) & ringMask)] += (float) (re[(size_t) k] * hann[(size_t) k] * norm);
}

} // namespace tt

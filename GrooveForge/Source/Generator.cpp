#include "Generator.h"

namespace gf
{

namespace
{
enum class FillStyle { None, Toms, SnareRoll, HatRoll };

struct GenreTraits
{
    GenreInfo info;
    FillStyle fill;
    bool crashAtStart;
};

const GenreTraits& traits (Genre g)
{
    static const GenreTraits t[(int) Genre::NumGenres] =
    {
        { { "House",          124.0, 0.54f, false }, FillStyle::SnareRoll, false },
        { { "Techno",         132.0, 0.50f, false }, FillStyle::SnareRoll, false },
        { { "Hip-Hop / Boom Bap", 90.0, 0.58f, false }, FillStyle::HatRoll, false },
        { { "Trap",           140.0, 0.50f, false }, FillStyle::HatRoll,   false },
        { { "Drum & Bass",    174.0, 0.50f, false }, FillStyle::Toms,      true  },
        { { "Breakbeat",      130.0, 0.53f, false }, FillStyle::Toms,      true  },
        { { "Rock",           120.0, 0.50f, false }, FillStyle::Toms,      true  },
        { { "Metal",          160.0, 0.50f, false }, FillStyle::Toms,      true  },
        { { "Funk",           104.0, 0.55f, false }, FillStyle::Toms,      true  },
        { { "Reggae (One Drop)", 76.0, 0.62f, true }, FillStyle::Toms,     false },
        { { "Afrobeat",       110.0, 0.54f, false }, FillStyle::Toms,      false },
        { { "Latin / Bossa",  120.0, 0.50f, false }, FillStyle::Toms,      false },
        { { "Jazz Swing",     150.0, 0.50f, true  }, FillStyle::Toms,      false },
        { { "Dubstep",        140.0, 0.50f, false }, FillStyle::Toms,      true  },
        { { "Reggaeton",       95.0, 0.50f, false }, FillStyle::SnareRoll, false },
        { { "UK Garage",      132.0, 0.62f, false }, FillStyle::SnareRoll, false },
    };
    return t[juce::jlimit (0, (int) Genre::NumGenres - 1, (int) g)];
}

std::vector<bool> euclid (int k, int n, int rotation)
{
    std::vector<bool> r ((size_t) juce::jmax (1, n), false);
    if (n <= 0) return r;
    k = juce::jlimit (0, n, k);
    for (int i = 0; i < n; ++i)
        if (((i * k) % n) < k)
            r[(size_t) (((i + rotation) % n + n) % n)] = true;
    return r;
}

struct Hit
{
    double step;         // in 16th steps from bar start (may be fractional = off-grid)
    float vel;
    double len = 1.0;    // in steps
    bool onGrid = true;  // swing only applies to on-grid hits
};

//==============================================================================
struct BarCtx
{
    const Meter& m;
    juce::Random& rng;
    float cx;
    int bar = 0;
    int numBars = 1;
    std::vector<Hit> hits;

    BarCtx (const Meter& meter, juce::Random& r, float complexity) : m (meter), rng (r), cx (complexity) {}

    bool chance (float p)                { return rng.nextFloat() < p; }
    float vary (float v, float amt = 0.06f) { return juce::jlimit (0.05f, 1.0f, v + (rng.nextFloat() * 2.0f - 1.0f) * amt); }
    int bars() const                     { return m.stepsPerBar; }

    void add (double step, float vel, double len = 1.0, bool grid = true)
    {
        if (step < 0.0 || step >= m.stepsPerBar) return;
        for (auto& h : hits)
            if (std::abs (h.step - step) < 0.01) { h.vel = juce::jmax (h.vel, vel); return; }
        hits.push_back ({ step, juce::jlimit (0.05f, 1.0f, vel), len, grid });
    }

    void removeRange (double from, double to)
    {
        hits.erase (std::remove_if (hits.begin(), hits.end(), [&] (const Hit& h) { return h.step >= from - 0.001 && h.step < to - 0.001; }),
                    hits.end());
    }

    bool has (double step) const
    {
        for (auto& h : hits) if (std::abs (h.step - step) < 0.01) return true;
        return false;
    }

    bool nearBackbeat (int step, int window = 0) const
    {
        for (auto b : m.backbeatSteps)
            if (std::abs (b - step) <= window) return true;
        return false;
    }

    // --- building blocks, all meter-aware -----------------------------------
    void onTactus (float vel)
    {
        for (int t = 0; t < m.numTactus(); ++t)
            add (m.tactusStart[(size_t) t], vary (m.isFirstTactusOfSuperGroup (t) ? vel : vel * 0.94f, 0.03f));
    }

    void onBackbeats (float vel)
    {
        for (auto b : m.backbeatSteps)
            add (b, vary (vel, 0.03f));
    }

    static std::vector<int> offbeatPositions (int len)
    {
        if (len >= 8)       return { len / 2 };
        if (len == 6)       return { 2, 4 };
        if (len == 4)       return { 2 };
        if (len == 3)       return { 2 };
        if (len == 2)       return { 1 };
        return {};
    }

    void offbeats (float vel, float prob = 1.0f)
    {
        for (int t = 0; t < m.numTactus(); ++t)
        {
            const auto pos = offbeatPositions (m.tactusLen[(size_t) t]);
            for (size_t i = 0; i < pos.size(); ++i)
                if (chance (prob))
                    add (m.tactusStart[(size_t) t] + pos[i], vary (i == 0 ? vel : vel * 0.8f));
        }
    }

    void eighths (float accent, float weak, float prob = 1.0f)
    {
        for (auto& s : m.steps)
            if (s.posInTactus % 2 == 0 && (s.tactusStart || chance (prob)))
                add (s.index, vary (s.tactusStart ? accent : weak));
    }

    void sixteenths (float accent, float eighth, float weak, float oddProb)
    {
        for (auto& s : m.steps)
        {
            if (s.posInTactus % 2 == 1)
            {
                if (chance (oddProb)) add (s.index, vary (weak));
            }
            else
                add (s.index, vary (s.tactusStart ? accent : eighth));
        }
    }

    /** Syncopated hits chosen by metrical weight. Avoids backbeats. */
    int probabilistic (float amount, float w4, float w3, float w2, float w1, int avoidWindow = 0, float vel = 0.85f)
    {
        int count = 0;
        for (auto& s : m.steps)
        {
            if (s.barStart || nearBackbeat (s.index, avoidWindow)) continue;
            const float w = s.weight == 4 ? w4 : s.weight == 3 ? w3 : s.weight == 2 ? w2 : w1;
            if (chance (amount * w))
            {
                add (s.index, vary (vel * (s.weight >= 3 ? 1.0f : 0.88f)));
                ++count;
            }
        }
        return count;
    }

    void ghosts (float prob, float vel)
    {
        for (auto& s : m.steps)
            if (s.posInTactus % 2 == 1 && ! nearBackbeat (s.index, 0) && ! has (s.index) && chance (prob))
                add (s.index, vary (vel, 0.07f));
    }

    void euclidean (int k, int rotation, float vel, float accentVel)
    {
        auto e = euclid (k, m.stepsPerBar, rotation);
        for (int i = 0; i < m.stepsPerBar; ++i)
            if (e[(size_t) i])
                add (i, vary (m.steps[(size_t) i].tactusStart ? accentVel : vel));
    }

    int density (float per16) const { return juce::jmax (1, juce::roundToInt (m.stepsPerBar * per16)); }
};

//==============================================================================
void house (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:      c.onTactus (1.0f);
                        if (c.cx > 0.75f && c.chance (0.35f)) c.add (m.stepsPerBar - 1, 0.55f);
                        break;
        case Snare:     if (c.cx > 0.4f) c.onBackbeats (0.5f); break;
        case Clap:      c.onBackbeats (0.9f); break;
        case Rim:       if (c.cx > 0.5f) c.euclidean (c.density (0.19f), 3 + c.rng.nextInt (4), 0.45f, 0.5f); break;
        case ClosedHat:
            for (auto& s : m.steps)
                if (! s.tactusStart && s.posInTactus * 2 != s.tactusLen && c.chance (0.25f + c.cx * 0.6f))
                    c.add (s.index, c.vary (s.posInTactus % 2 == 0 ? 0.55f : 0.42f));
            break;
        case OpenHat:   c.offbeats (0.8f); break;
        case Perc:      if (c.cx > 0.3f) c.sixteenths (0.5f, 0.42f, 0.32f, c.cx); break;
        case MidTom:
        case HighTom:   if (c.cx > 0.7f) for (auto& s : m.steps) if (s.posInTactus % 2 == 1 && c.chance (0.08f)) c.add (s.index, c.vary (0.55f)); break;
        default: break;
    }
}

void techno (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:      c.onTactus (1.0f);
                        if (c.cx > 0.6f && c.chance ((c.cx - 0.6f) * 1.5f))
                        {
                            const int t = c.rng.nextInt (m.numTactus());
                            if (m.tactusLen[(size_t) t] >= 4) c.add (m.tactusStart[(size_t) t] + m.tactusLen[(size_t) t] - 1, 0.5f);
                        }
                        break;
        case Clap:      if (c.cx > 0.25f) c.onBackbeats (0.8f); break;
        case Snare:     if (c.cx > 0.7f) c.euclidean (c.density (0.12f), 5, 0.4f, 0.45f); break;
        case Rim:       c.euclidean (c.density ((3.0f + c.cx * 3.0f) / 16.0f), 2 + c.rng.nextInt (3), 0.55f, 0.6f); break;
        case ClosedHat: c.sixteenths (0.5f, 0.6f, 0.4f, 0.5f + c.cx * 0.5f); break;
        case OpenHat:   c.offbeats (0.85f); break;
        case Ride:      if (c.cx > 0.5f) c.offbeats (0.45f); break;
        case Perc:      if (c.cx > 0.35f) c.euclidean (c.density (5.0f / 16.0f), 2, 0.42f, 0.5f); break;
        case LowTom:    if (c.cx > 0.65f) c.euclidean (juce::jmax (2, c.density (0.16f)), 5 + c.rng.nextInt (6), 0.5f, 0.5f); break;
        default: break;
    }
}

void hipHop (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:
        {
            c.add (0, 1.0f);
            if (c.chance (0.75f)) c.add (m.halfTimeStep + 2, 0.9f);
            int n = c.probabilistic (0.25f + c.cx * 0.35f, 0.55f, 0.15f, 0.35f, 0.3f, 0);
            if (n == 0 && m.stepsPerBar > 6) c.add (m.stepsPerBar - 6, 0.8f);
            break;
        }
        case Snare:     c.onBackbeats (0.95f); c.ghosts (c.cx * 0.3f, 0.28f); break;
        case Clap:      if (c.cx > 0.6f) c.onBackbeats (0.5f); break;
        case Rim:       if (c.cx > 0.7f) for (auto& s : m.steps) if (s.posInTactus % 2 == 1 && c.chance (0.1f)) c.add (s.index, c.vary (0.45f)); break;
        case ClosedHat:
            c.eighths (0.75f, 0.6f);
            if (c.cx > 0.5f)
                for (auto& s : m.steps) if (s.posInTactus % 2 == 1 && c.chance ((c.cx - 0.5f) * 0.6f)) c.add (s.index, c.vary (0.4f));
            break;
        case OpenHat:   if (c.chance (0.35f + c.cx * 0.3f)) c.add (m.stepsPerBar - 2, 0.6f, 2.0); break;
        case Perc:      if (c.cx > 0.65f) c.offbeats (0.35f); break;
        default: break;
    }
}

void trap (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:
        {
            c.add (0, 1.0f, 3.0);
            int n = 0;
            for (auto& s : m.steps)
            {
                if (s.barStart || std::abs (s.index - m.halfTimeStep) <= 1) continue;
                const float w = s.weight >= 3 ? 0.35f : s.weight == 2 ? 0.3f : 0.22f;
                if (c.chance ((0.15f + c.cx * 0.35f) * w * 2.0f)) { c.add (s.index, c.vary (0.9f), 2.0); ++n; }
            }
            if (n == 0) c.add (juce::jmin (m.stepsPerBar - 1, m.halfTimeStep + 3), 0.85f, 2.0);
            break;
        }
        case Snare:     c.add (m.halfTimeStep, 1.0f);
                        if (c.cx > 0.7f && c.chance (0.3f)) c.add (m.stepsPerBar - 1, 0.5f);
                        break;
        case Clap:      c.add (m.halfTimeStep, 0.9f); break;
        case ClosedHat:
        {
            if (c.cx > 0.4f) c.sixteenths (0.75f, 0.65f, 0.5f, 1.0f);
            else             c.eighths (0.75f, 0.65f);

            const int rolls = 1 + (int) (c.cx * 3.0f * c.rng.nextFloat());
            for (int r = 0; r < rolls; ++r)
            {
                const int s = 2 * (1 + c.rng.nextInt (juce::jmax (1, m.stepsPerBar / 2 - 1)));
                const int type = c.rng.nextInt (4);
                const double span = type == 3 ? 4.0 : 2.0;
                const int count = type == 0 ? 4 : type == 1 ? 3 : type == 2 ? 6 : 8;
                if (s + span > m.stepsPerBar) continue;
                c.removeRange (s, s + span);
                for (int i = 0; i < count; ++i)
                    c.add (s + span * i / count, 0.45f + 0.45f * (float) i / (float) count, span / count, false);
            }
            break;
        }
        case OpenHat:   if (c.chance (0.3f)) { auto& s = m.steps[(size_t) (c.rng.nextInt (m.stepsPerBar / 2) * 2)]; if (! s.barStart) c.add (s.index, 0.6f, 2.0); } break;
        case Rim:       if (c.cx > 0.5f) for (auto& s : m.steps) if (s.posInTactus % 2 == 1 && c.chance (0.08f)) c.add (s.index, c.vary (0.5f)); break;
        default: break;
    }
}

void dnb (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:
            c.add (0, 1.0f);
            if (m.superGroupSizes.size() > 1)
            {
                for (size_t g = 1; g < m.superGroupSizes.size(); ++g)
                    c.add (m.tactusStart[(size_t) m.superGroupFirstTactus[g]] + 2, 0.9f);
            }
            else if (m.numTactus() >= 3)
                c.add (m.tactusStart[(size_t) m.numTactus() - 2] + 2, 0.9f);
            c.probabilistic (c.cx * 0.2f, 0.4f, 0.2f, 0.4f, 0.3f, 1);
            break;
        case Snare:     c.onBackbeats (1.0f); c.ghosts (0.12f + c.cx * 0.3f, 0.25f); break;
        case Clap:      if (c.cx > 0.8f) c.onBackbeats (0.4f); break;
        case ClosedHat:
            c.eighths (0.7f, 0.55f);
            for (auto& s : m.steps) if (s.posInTactus % 2 == 1 && c.chance (c.cx * 0.4f)) c.add (s.index, c.vary (0.38f));
            break;
        case Ride:      if (c.cx > 0.55f) c.offbeats (0.5f); break;
        case OpenHat:   if (! m.backbeatSteps.empty() && c.chance (0.5f)) c.add (m.backbeatSteps.back() - 2, 0.6f, 2.0); break;
        case Perc:      if (c.cx > 0.5f) c.sixteenths (0.45f, 0.4f, 0.3f, 0.8f); break;
        default: break;
    }
}

void breakbeat (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:
            c.add (0, 1.0f);
            if (m.tactusLen[0] >= 4) c.add (2, 0.85f);
            for (size_t g = 1; g < m.superGroupSizes.size(); ++g)
                c.add (m.tactusStart[(size_t) m.superGroupFirstTactus[g]] + 2, 0.9f);
            c.probabilistic (c.cx * 0.15f, 0.3f, 0.2f, 0.4f, 0.3f, 1);
            break;
        case Snare:
            for (size_t i = 0; i < m.backbeatSteps.size(); ++i)
            {
                const int b = m.backbeatSteps[i];
                c.add (b, c.vary (1.0f, 0.03f));
                if (b + 3 < m.stepsPerBar && ! m.steps[(size_t) (b + 3)].tactusStart && c.chance (0.7f)) c.add (b + 3, c.vary (0.55f));
                if (i > 0 && c.chance (0.6f)) c.add (b - 3, c.vary (0.6f));
            }
            if (c.chance (0.3f + c.cx * 0.4f)) c.add (m.stepsPerBar - 1, 0.5f);
            c.ghosts (c.cx * 0.15f, 0.25f);
            break;
        case ClosedHat: if (c.cx <= 0.6f) c.eighths (0.7f, 0.55f); break;
        case Ride:      if (c.cx > 0.6f) c.eighths (0.6f, 0.5f); break;
        case OpenHat:   if (c.cx > 0.5f && c.chance (0.5f)) c.add (m.stepsPerBar - 2, 0.6f, 2.0); break;
        default: break;
    }
}

void rock (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:
            for (int t = 0; t < m.numTactus(); ++t)
                if (m.isFirstTactusOfSuperGroup (t)) c.add (m.tactusStart[(size_t) t], c.vary (0.95f, 0.03f));
            for (auto& s : m.steps)
                if (! s.tactusStart && (s.posInTactus % 2 == 0 || c.cx > 0.8f) && ! c.nearBackbeat (s.index, 0)
                    && c.chance ((0.12f + c.cx * 0.3f) * (s.posInTactus % 2 == 0 ? 1.0f : 0.4f)))
                    c.add (s.index, c.vary (0.8f));
            break;
        case Snare:     c.onBackbeats (1.0f); if (c.cx > 0.6f) c.ghosts (0.15f, 0.22f); break;
        case ClosedHat: if (c.cx > 0.8f) c.sixteenths (0.8f, 0.65f, 0.45f, 1.0f); else c.eighths (0.8f, 0.6f); break;
        case OpenHat:   if (c.cx > 0.5f && c.chance (0.4f)) c.add (m.stepsPerBar - 2, 0.6f, 2.0); break;
        default: break;
    }
}

void metal (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:
            if (c.cx > 0.45f)
                for (auto& s : m.steps) c.add (s.index, c.vary (s.posInTactus % 2 == 0 ? 0.9f : 0.78f, 0.04f));
            else
                for (auto& s : m.steps)   // gallop: x.xx per tactus
                    if (s.posInTactus == 0 || (s.posInTactus >= s.tactusLen - 2)) c.add (s.index, c.vary (0.85f));
            break;
        case Snare:
            c.onBackbeats (1.0f);
            if (c.cx > 0.85f) for (auto& s : m.steps) if (s.posInTactus % 2 == 1 && c.chance (0.5f)) c.add (s.index, c.vary (0.8f));
            break;
        case Crash:     if (c.cx > 0.6f) c.onTactus (0.7f); break;
        case ClosedHat: if (c.cx <= 0.6f) c.eighths (0.85f, 0.65f); break;
        case Ride:      if (c.cx > 0.6f) c.offbeats (0.55f); break;
        default: break;
    }
}

void funk (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:      c.add (0, 1.0f); c.probabilistic (0.25f + c.cx * 0.25f, 0.5f, 0.3f, 0.3f, 0.38f, 0); break;
        case Snare:     c.onBackbeats (0.95f); c.ghosts (0.25f + c.cx * 0.4f, 0.24f); break;
        case ClosedHat: c.sixteenths (0.75f, 0.6f, 0.35f, 0.7f + c.cx * 0.3f); break;
        case OpenHat:   for (auto b : m.backbeatSteps) if (b > 0 && c.chance (0.55f)) c.add (b - 1, 0.65f); break;
        case Perc:      if (c.cx > 0.6f) c.sixteenths (0.4f, 0.35f, 0.25f, 0.9f); break;
        default: break;
    }
}

void reggae (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:      if (c.cx > 0.8f) c.onTactus (0.9f); else c.add (m.halfTimeStep, 1.0f); break;
        case Rim:       c.add (m.halfTimeStep, 0.9f);
                        if (c.cx > 0.5f && c.chance (0.4f)) c.add (m.stepsPerBar - 2, 0.55f);
                        break;
        case Snare:     if (c.cx > 0.6f && c.chance (0.3f)) c.add (m.halfTimeStep - 1, 0.35f); break;
        case ClosedHat: c.eighths (0.7f, 0.5f); break;
        case OpenHat:   if (c.chance (0.4f) && m.halfTimeStep >= 2) c.add (m.halfTimeStep - 2, 0.55f, 2.0); break;
        case Perc:      if (c.cx > 0.5f) c.offbeats (0.4f); break;
        case LowTom:    if (c.cx > 0.7f && c.chance (0.4f)) c.add (m.stepsPerBar - 1, 0.6f); break;
        default: break;
    }
}

void afrobeat (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:      c.add (0, 1.0f); c.probabilistic (0.3f + c.cx * 0.15f, 0.4f, 0.3f, 0.35f, 0.3f, 0); break;
        case Snare:     c.onBackbeats (0.7f); c.ghosts (0.3f + c.cx * 0.3f, 0.3f); break;
        case ClosedHat: c.sixteenths (0.7f, 0.55f, 0.35f, 0.8f); break;
        case OpenHat:   c.offbeats (0.65f, 0.6f); break;
        case Rim:       c.euclidean (c.density (5.0f / 16.0f), (c.bar % 2) * 3, 0.6f, 0.65f); break;
        case Perc:      c.sixteenths (0.5f, 0.4f, 0.3f, 0.9f); break;
        case Ride:      if (c.cx > 0.7f) c.euclidean (c.density (7.0f / 16.0f), 1, 0.5f, 0.55f); break;
        case LowTom:
        case MidTom:
            for (int t = 0; t < m.numTactus(); ++t)
                if (c.chance (0.2f + c.cx * 0.25f))
                    c.add (m.tactusStart[(size_t) t] + 1 + 2 * c.rng.nextInt (juce::jmax (1, m.tactusLen[(size_t) t] / 2)), c.vary (0.55f));
            break;
        default: break;
    }
}

void latin (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:
            for (int t = 0; t < m.numTactus(); ++t)
            {
                const int len = m.tactusLen[(size_t) t];
                c.add (m.tactusStart[(size_t) t], c.vary (0.85f));
                if (len >= 4) c.add (m.tactusStart[(size_t) t] + (len == 6 ? 4 : len - 1), c.vary (0.6f));
            }
            break;
        case Rim:       c.euclidean (c.density (5.0f / 16.0f), (c.bar % 2 == 0) ? 0 : m.stepsPerBar / 2, 0.7f, 0.75f); break;
        case ClosedHat: if (c.cx <= 0.5f) c.eighths (0.55f, 0.45f); break;
        case Perc:      c.sixteenths (0.55f, 0.45f, 0.35f, 1.0f); break;
        case LowTom:
            for (size_t g = 0; g < m.superGroupSizes.size(); ++g)
            {
                const int end = g + 1 < m.superGroupSizes.size() ? m.tactusStart[(size_t) m.superGroupFirstTactus[g + 1]] : m.stepsPerBar;
                if (c.cx > 0.5f && c.chance (0.45f)) c.add (end - 1, c.vary (0.55f));
            }
            break;
        case Ride:      if (c.cx > 0.7f) c.eighths (0.5f, 0.4f); break;
        default: break;
    }
}

void jazz (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Ride:
            for (int t = 0; t < m.numTactus(); ++t)
            {
                const int start = m.tactusStart[(size_t) t];
                const int len   = m.tactusLen[(size_t) t];
                c.add (start, c.vary (m.isFirstTactusOfSuperGroup (t) ? 0.75f : 0.85f, 0.04f), 2.0);
                if (! m.isFirstTactusOfSuperGroup (t) || (c.cx > 0.6f && c.chance (0.4f)))
                {
                    // the triplet "skip" note - deliberately off the straight grid
                    if (len % 4 == 0)   c.add (start + len * 2.0 / 3.0, c.vary (0.6f), 1.0, false);
                    else if (len == 6)  c.add (start + 4, c.vary (0.6f));
                }
            }
            break;
        case ClosedHat: c.onBackbeats (0.6f); break;
        case Kick:
            if (c.cx > 0.3f) c.onTactus (0.22f);
            for (int t = 0; t < m.numTactus(); ++t)
                if (c.chance (c.cx * 0.25f))
                    c.add (m.tactusStart[(size_t) t] + m.tactusLen[(size_t) t] * 2.0 / 3.0, c.vary (0.6f), 1.0, false);
            break;
        case Snare:
            for (int t = 0; t < m.numTactus(); ++t)
                if (c.chance (0.15f + c.cx * 0.35f))
                    c.add (m.tactusStart[(size_t) t] + m.tactusLen[(size_t) t] * (double) c.rng.nextInt (3) / 3.0,
                           0.25f + c.rng.nextFloat() * 0.3f, 1.0, false);
            break;
        default: break;
    }
}

void dubstep (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:
            c.add (0, 1.0f, 2.0);
            for (auto& s : m.steps)
                if (! s.barStart && std::abs (s.index - m.halfTimeStep) > 2 && c.chance ((0.1f + c.cx * 0.2f) * (s.weight >= 2 ? 0.5f : 0.35f)))
                    c.add (s.index, c.vary (0.85f));
            break;
        case Snare:     c.add (m.halfTimeStep, 1.0f); break;
        case Clap:      c.add (m.halfTimeStep, 0.8f); break;
        case ClosedHat:
            c.eighths (0.6f, 0.5f, 0.8f);
            if (c.cx > 0.5f)
            {
                const int t = c.rng.nextInt (m.numTactus());
                const int start = m.tactusStart[(size_t) t], len = m.tactusLen[(size_t) t];
                c.removeRange (start, start + len);
                for (int k = 0; k < 3; ++k) c.add (start + len * k / 3.0, 0.55f, 1.0, false);
            }
            break;
        case OpenHat:   if (c.chance (0.4f)) c.add (m.stepsPerBar - 2, 0.55f, 2.0); break;
        case Perc:      if (c.cx > 0.6f) for (auto& s : m.steps) if (c.chance (0.25f)) c.add (s.index, c.vary (0.3f)); break;
        default: break;
    }
}

void dembowSteps (BarCtx& c, float vel)
{
    auto& m = c.m;
    for (int t = 0; t < m.numTactus(); ++t)
    {
        const int start = m.tactusStart[(size_t) t], len = m.tactusLen[(size_t) t];
        if (m.isFirstTactusOfSuperGroup (t) || len < 4) c.add (start + len - 1, c.vary (vel));
        else                                            c.add (start + len / 2, c.vary (vel));
    }
}

void reggaeton (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:      c.onTactus (1.0f); break;
        case Snare:     dembowSteps (c, 0.9f); break;
        case Clap:      if (c.cx > 0.4f) dembowSteps (c, 0.6f); break;
        case ClosedHat: c.eighths (0.6f, 0.5f);
                        if (c.cx > 0.5f) for (auto& s : m.steps) if (s.posInTactus % 2 == 1 && c.chance (0.5f)) c.add (s.index, c.vary (0.35f));
                        break;
        case Rim:       if (c.cx > 0.6f) for (auto& s : m.steps) if (s.posInTactus % 2 == 1 && c.chance (0.1f)) c.add (s.index, c.vary (0.5f)); break;
        case Perc:      if (c.cx > 0.45f) c.sixteenths (0.45f, 0.4f, 0.3f, 0.8f); break;
        default: break;
    }
}

void ukGarage (int lane, BarCtx& c)
{
    auto& m = c.m;
    switch (lane)
    {
        case Kick:
            c.add (0, 1.0f);
            for (size_t g = 1; g < m.superGroupSizes.size(); ++g)
                c.add (m.tactusStart[(size_t) m.superGroupFirstTactus[g]] + 2, 0.85f);
            if (m.superGroupSizes.size() == 1 && m.numTactus() >= 2) c.add (m.tactusStart.back() + 2, 0.85f);
            c.probabilistic (c.cx * 0.2f, 0.3f, 0.2f, 0.4f, 0.3f, 1);
            break;
        case Snare:     c.onBackbeats (0.85f); break;
        case Clap:      c.onBackbeats (0.9f); break;
        case ClosedHat:
            for (auto& s : m.steps)
            {
                if (s.tactusStart) continue;
                if (s.posInTactus % 2 == 1) { if (c.chance (0.5f + c.cx * 0.4f)) c.add (s.index, c.vary (0.5f)); }
                else c.add (s.index, c.vary (0.6f));
            }
            break;
        case OpenHat:   c.offbeats (0.7f); break;
        case Rim:       if (c.cx > 0.4f) c.euclidean (c.density (3.0f / 16.0f), 3, 0.5f, 0.5f); break;
        case Perc:      if (c.cx > 0.5f) c.sixteenths (0.4f, 0.35f, 0.3f, 0.7f); break;
        default: break;
    }
}

void generateLaneBar (Genre g, int lane, BarCtx& c)
{
    switch (g)
    {
        case Genre::House:       house (lane, c); break;
        case Genre::Techno:      techno (lane, c); break;
        case Genre::HipHop:      hipHop (lane, c); break;
        case Genre::Trap:        trap (lane, c); break;
        case Genre::DrumAndBass: dnb (lane, c); break;
        case Genre::Breakbeat:   breakbeat (lane, c); break;
        case Genre::Rock:        rock (lane, c); break;
        case Genre::Metal:       metal (lane, c); break;
        case Genre::Funk:        funk (lane, c); break;
        case Genre::Reggae:      reggae (lane, c); break;
        case Genre::Afrobeat:    afrobeat (lane, c); break;
        case Genre::Latin:       latin (lane, c); break;
        case Genre::Jazz:        jazz (lane, c); break;
        case Genre::Dubstep:     dubstep (lane, c); break;
        case Genre::Reggaeton:   reggaeton (lane, c); break;
        case Genre::UKGarage:    ukGarage (lane, c); break;
        case Genre::NumGenres:   break;
    }
}

//==============================================================================
struct FillHit { int lane; double step; float vel; };

std::vector<FillHit> makeFill (FillStyle style, const Meter& m, float cx, int seed, double& regionStart)
{
    juce::Random rng ((juce::int64) seed * 92821 + 17);
    int startTactus = m.numTactus() - 1;
    if (m.numTactus() >= 2 && (m.tactusLen.back() < 4 || (cx > 0.65f && m.numTactus() >= 3)))
        startTactus = m.numTactus() - 2;

    const int start = m.tactusStart[(size_t) startTactus];
    const int len = m.stepsPerBar - start;
    regionStart = start;

    std::vector<FillHit> out;

    switch (style)
    {
        case FillStyle::Toms:
        {
            const int lanes[] = { Snare, HighTom, MidTom, LowTom };
            for (int s = start; s < m.stepsPerBar; ++s)
            {
                const bool even = (s - start) % 2 == 0;
                if (! even && (cx < 0.4f || rng.nextFloat() > 0.8f)) continue;
                const float frac = (float) (s - start) / (float) len;
                const int lane = lanes[juce::jlimit (0, 3, (int) (frac * 4.0f))];
                out.push_back ({ lane, (double) s, 0.65f + 0.3f * frac + rng.nextFloat() * 0.05f });
            }
            break;
        }
        case FillStyle::SnareRoll:
            for (int s = start; s < m.stepsPerBar; ++s)
            {
                const float frac = (float) (s - start) / (float) len;
                out.push_back ({ Snare, (double) s, 0.3f + 0.65f * frac });
                if (cx > 0.6f && frac > 0.5f)
                    out.push_back ({ Snare, s + 0.5, 0.3f + 0.6f * frac });
                if (s % 2 == 0)
                    out.push_back ({ Clap, (double) s, 0.4f + 0.5f * frac });
            }
            break;
        case FillStyle::HatRoll:
        {
            const int n = len * 2;
            for (int i = 0; i < n; ++i)
                out.push_back ({ ClosedHat, start + i * 0.5, 0.35f + 0.6f * (float) i / (float) n });
            break;
        }
        case FillStyle::None: break;
    }

    return out;
}

bool laneAffectedByFill (FillStyle style, int lane)
{
    switch (style)
    {
        case FillStyle::Toms:      return lane == Snare || lane == HighTom || lane == MidTom || lane == LowTom
                                       || lane == ClosedHat || lane == OpenHat || lane == Ride || lane == Clap;
        case FillStyle::SnareRoll: return lane == Snare || lane == Clap;
        case FillStyle::HatRoll:   return lane == ClosedHat || lane == OpenHat;
        case FillStyle::None:      return false;
    }
    return false;
}

double applySwing (const Meter& m, double step, float swing, bool eighthSwing)
{
    const int barStep = (int) step;
    if (std::abs (step - barStep) > 0.001 || barStep >= m.stepsPerBar)
        return step;

    const auto& s = m.steps[(size_t) barStep];
    const double amount = juce::jlimit (0.0, 0.5, (double) swing - 0.5) * 2.0;   // 0..0.5

    if (eighthSwing)
    {
        if (s.tactusLen % 4 != 0) return step;          // compound beats already swing
        const double d = amount * 2.0;                  // up to 1 step
        const int p = s.posInTactus % 4;
        const double base = step - p;
        return p < 2 ? base + p * (2.0 + d) / 2.0
                     : base + 2.0 + d + (p - 2) * (2.0 - d) / 2.0;
    }

    if (s.tactusLen % 2 != 0 || s.posInTactus % 2 == 0)
        return step;
    return step + amount;
}

/** Deliberate genre-specific micro-timing (in steps): the "feel". */
double feelOffset (Genre g, int lane)
{
    if (g == Genre::HipHop)   return lane == Snare || lane == Clap ? 0.10 : lane == ClosedHat ? -0.03 : lane == Kick ? 0.03 : 0.0;
    if (g == Genre::Jazz)     return lane == Ride ? -0.04 : 0.0;
    if (g == Genre::Reggae)   return lane == Rim || lane == Kick ? 0.05 : 0.0;
    if (g == Genre::Funk)     return lane == Snare ? 0.03 : 0.0;
    if (g == Genre::Afrobeat) return lane == Perc ? -0.04 : 0.0;
    return 0.0;
}

} // namespace

//==============================================================================
const GenreInfo& Generator::info (Genre g) { return traits (g).info; }

juce::StringArray Generator::genreNames()
{
    juce::StringArray s;
    for (int i = 0; i < (int) Genre::NumGenres; ++i)
        s.add (traits ((Genre) i).info.name);
    return s;
}

void Generator::generate (Pattern& pattern, const GeneratorParams& p,
                          const std::array<bool, NumLanes>& lanesToGenerate)
{
    const auto meter = Meter::create (pattern.numerator, pattern.denominator, pattern.grouping);
    pattern.grouping = meter.grouping;

    const auto& tr = traits (p.genre);

    pattern.notes.erase (std::remove_if (pattern.notes.begin(), pattern.notes.end(),
                                         [&] (const DrumNote& n) { return lanesToGenerate[(size_t) n.lane]; }),
                         pattern.notes.end());

    const double stepBeats = 4.0 / 16.0;
    const double barBeats = pattern.beatsPerBar();
    const bool hasFill = p.fills && pattern.bars >= 2 && tr.fill != FillStyle::None;

    // Decide which bars are variations (shared across lanes so the layers stay coherent).
    std::vector<int> barKey ((size_t) pattern.bars, 0);
    {
        juce::Random vr ((juce::int64) p.seed * 31337 + 5);
        for (int b = 1; b < pattern.bars; ++b)
        {
            const bool isVariant = vr.nextFloat() < p.variation * (b % 2 == 1 ? 1.0f : 0.6f);
            barKey[(size_t) b] = isVariant ? b : 0;
        }
    }

    for (int lane = 0; lane < NumLanes; ++lane)
    {
        if (! lanesToGenerate[(size_t) lane])
            continue;

        juce::Random humanRng ((juce::int64) p.seed * 7919 + lane * 104729 + 99);

        for (int b = 0; b < pattern.bars; ++b)
        {
            juce::Random rng ((juce::int64) p.seed * 15485863 + lane * 104729 + barKey[(size_t) b] * 7349 + (int) p.genre * 31);
            BarCtx ctx (meter, rng, p.complexity);
            ctx.bar = b;
            ctx.numBars = pattern.bars;

            generateLaneBar (p.genre, lane, ctx);

            if (lane == Crash && b == 0 && tr.crashAtStart)
                ctx.add (0, 0.85f, 4.0);

            if (hasFill && b == pattern.bars - 1)
            {
                double regionStart = 0;
                auto fill = makeFill (tr.fill, meter, p.complexity, p.seed + b, regionStart);
                if (laneAffectedByFill (tr.fill, lane))
                {
                    ctx.removeRange (regionStart, meter.stepsPerBar);
                    for (auto& f : fill)
                        if (f.lane == lane)
                            ctx.add (f.step, f.vel, 1.0, std::abs (f.step - std::round (f.step)) < 0.001);
                }
            }

            for (auto& h : ctx.hits)
            {
                double step = h.onGrid ? applySwing (meter, h.step, p.swing, tr.info.eighthSwing) : h.step;
                step += feelOffset (p.genre, lane) * (0.5 + p.humanize);

                // humanize: small random timing drift (never on the bar downbeat of bar 0)
                if (p.humanize > 0.0f && ! (b == 0 && h.step < 0.001))
                {
                    const double g = (humanRng.nextFloat() + humanRng.nextFloat() + humanRng.nextFloat() - 1.5f) / 1.5f;
                    step += g * p.humanize * 0.18;
                }

                float vel = h.vel;
                if (p.humanize > 0.0f)
                    vel = juce::jlimit (0.05f, 1.0f, vel + (humanRng.nextFloat() * 2.0f - 1.0f) * p.humanize * 0.12f);

                DrumNote n;
                n.lane = lane;
                n.beat = juce::jmax (0.0, b * barBeats + step * stepBeats);
                n.length = h.len * stepBeats;
                n.velocity = vel;
                pattern.notes.push_back (n);
            }
        }
    }

    pattern.sortNotes();
}

} // namespace gf

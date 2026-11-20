#include "Meter.h"

namespace gf
{

std::vector<int> Meter::splitIntoGroups (int n)
{
    switch (n)
    {
        case 1: return { 1 };
        case 2: return { 2 };
        case 3: return { 3 };
        case 4: return { 2, 2 };
        case 5: return { 3, 2 };
        case 6: return { 3, 3 };
        case 7: return { 2, 2, 3 };
        default: break;
    }

    std::vector<int> g;
    int remaining = n;

    while (remaining > 0)
    {
        if (remaining == 3 || remaining == 5) { g.push_back (3); remaining -= 3; }
        else                                  { g.push_back (2); remaining -= 2; }
    }

    return g;
}

static juce::String joinGroups (const std::vector<int>& g)
{
    juce::StringArray parts;
    for (auto v : g)
        parts.add (juce::String (v));
    return parts.joinIntoString ("+");
}

juce::String Meter::defaultGrouping (int num, int den)
{
    num = juce::jmax (1, num);

    if (den <= 4)
        return joinGroups (splitIntoGroups (num));

    // Additive / compound meters: tactus beats of 2 or 3 subdivisions.
    if (num % 3 == 0)
        return joinGroups (std::vector<int> ((size_t) (num / 3), 3));   // 3/8, 6/8, 9/8, 12/8 ...

    switch (num)
    {
        case 1:  return "1";
        case 2:  return "2";
        case 4:  return "2+2";
        case 5:  return "3+2";
        case 7:  return "2+2+3";
        case 8:  return "3+3+2";
        case 10: return "3+3+2+2";
        case 11: return "3+3+3+2";
        case 13: return "3+3+3+2+2";
        default: break;
    }

    std::vector<int> g;
    int remaining = num;
    while (remaining > 0)
    {
        if (remaining % 3 == 0 || remaining > 4) { g.push_back (3); remaining -= 3; }
        else                                     { g.push_back (2); remaining -= 2; }
    }
    return joinGroups (g);
}

std::vector<int> Meter::parseGrouping (const juce::String& spec)
{
    std::vector<int> g;
    juce::StringArray tokens;
    tokens.addTokens (spec, "+,; ", {});
    tokens.removeEmptyStrings();

    for (auto& t : tokens)
    {
        const int v = t.getIntValue();
        if (v > 0)
            g.push_back (v);
    }
    return g;
}

Meter Meter::create (int num, int den, const juce::String& spec)
{
    Meter m;
    num = juce::jlimit (1, 32, num);
    if (den != 2 && den != 4 && den != 8 && den != 16)
        den = 4;

    m.numerator = num;
    m.denominator = den;
    m.stepsPerCount = 16 / den;
    m.stepsPerBar = num * m.stepsPerCount;

    auto groups = parseGrouping (spec);
    int total = 0;
    for (auto g : groups)
        total += g;

    if (groups.empty() || total != num)
        groups = parseGrouping (defaultGrouping (num, den));

    m.grouping = joinGroups (groups);

    if (den <= 4)
    {
        // Simple meter: every count is a tactus beat, the spec groups the beats.
        for (int i = 0; i < num; ++i)
        {
            m.tactusStart.push_back (i * m.stepsPerCount);
            m.tactusLen.push_back (m.stepsPerCount);
        }
        m.superGroupSizes = groups;
        m.compound = false;
    }
    else
    {
        // Compound / additive: each group is one (possibly uneven) tactus beat.
        int pos = 0;
        bool allThrees = true;
        for (auto g : groups)
        {
            m.tactusStart.push_back (pos);
            m.tactusLen.push_back (g * m.stepsPerCount);
            pos += g * m.stepsPerCount;
            allThrees = allThrees && g == 3;
        }
        m.compound = allThrees;
        m.superGroupSizes = splitIntoGroups ((int) groups.size());
    }

    {
        int t = 0;
        for (auto s : m.superGroupSizes)
        {
            m.superGroupFirstTactus.push_back (t);
            t += s;
        }
    }

    // Backbeats: the last tactus of every super-group that has at least two beats.
    for (size_t g = 0; g < m.superGroupSizes.size(); ++g)
        if (m.superGroupSizes[g] >= 2)
            m.backbeatSteps.push_back (m.tactusStart[(size_t) (m.superGroupFirstTactus[g] + m.superGroupSizes[g] - 1)]);

    if (m.backbeatSteps.empty() && m.numTactus() > 1)
        m.backbeatSteps.push_back (m.tactusStart.back());

    // Half-time backbeat: the tactus start nearest the middle of the bar (later wins ties).
    {
        const double mid = m.stepsPerBar * 0.5;
        int best = m.numTactus() > 1 ? m.tactusStart[1] : m.stepsPerBar / 2;
        double bestDist = 1.0e9;
        for (int t = 1; t < m.numTactus(); ++t)
        {
            const double d = std::abs (m.tactusStart[(size_t) t] - mid);
            if (d <= bestDist) { bestDist = d; best = m.tactusStart[(size_t) t]; }
        }
        m.halfTimeStep = best;
    }

    m.steps.resize ((size_t) m.stepsPerBar);
    for (int t = 0; t < m.numTactus(); ++t)
    {
        for (int p = 0; p < m.tactusLen[(size_t) t]; ++p)
        {
            auto& st = m.steps[(size_t) (m.tactusStart[(size_t) t] + p)];
            st.index = m.tactusStart[(size_t) t] + p;
            st.tactus = t;
            st.posInTactus = p;
            st.tactusLen = m.tactusLen[(size_t) t];
            st.tactusStart = p == 0;
            st.barStart = st.index == 0;
            st.superStart = p == 0 && m.isFirstTactusOfSuperGroup (t);
            st.backbeat = p == 0 && std::find (m.backbeatSteps.begin(), m.backbeatSteps.end(), st.index) != m.backbeatSteps.end();
            st.weight = st.barStart ? 5 : st.superStart ? 4 : st.tactusStart ? 3 : (st.index % 2 == 0 ? 2 : 1);
        }
    }

    return m;
}

int Meter::superGroupOfTactus (int t) const
{
    for (int g = (int) superGroupSizes.size() - 1; g >= 0; --g)
        if (t >= superGroupFirstTactus[(size_t) g])
            return g;
    return 0;
}

bool Meter::isFirstTactusOfSuperGroup (int t) const
{
    return std::find (superGroupFirstTactus.begin(), superGroupFirstTactus.end(), t) != superGroupFirstTactus.end();
}

bool Meter::isLastTactusOfSuperGroup (int t) const
{
    const auto g = (size_t) superGroupOfTactus (t);
    return t == superGroupFirstTactus[g] + superGroupSizes[g] - 1;
}

} // namespace gf

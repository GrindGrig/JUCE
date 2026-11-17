#include "Modulation.h"

namespace sw
{

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr float previewRandom[] { 0.55f, -0.35f, 0.9f, -0.75f };

    void sortPoints (ShapePoints& p)
    {
        std::sort (p.begin(), p.end(), [] (auto a, auto b) { return a.x < b.x; });
    }
}

ShapePoints defaultCustomPoints()
{
    return { { 0.0f, -1.0f }, { 0.1f, 1.0f }, { 0.42f, 0.1f }, { 0.68f, 0.45f }, { 0.9f, -0.55f } };
}

ShapePoints shapeToPoints (int shape)
{
    switch (shape)
    {
        case triangle: return { { 0.0f, -1.0f }, { 0.5f, 1.0f } };
        case sawUp:    return { { 0.0f, -1.0f }, { 0.995f, 1.0f } };
        case sawDown:  return { { 0.0f, 1.0f }, { 0.995f, -1.0f } };
        case square:   return { { 0.0f, 1.0f }, { 0.495f, 1.0f }, { 0.5f, -1.0f }, { 0.995f, -1.0f } };

        case sampleHold:
        case smoothRandom:
        {
            ShapePoints p;

            for (int i = 0; i < 4; ++i)
            {
                p.push_back ({ (float) i / 4.0f, previewRandom[i] });

                if (shape == sampleHold)
                    p.push_back ({ (float) i / 4.0f + 0.245f, previewRandom[i] });
            }

            return p;
        }

        case custom:
            return defaultCustomPoints();

        case sine:
        default:
        {
            ShapePoints p;

            for (int i = 0; i < 16; ++i)
                p.push_back ({ (float) i / 16.0f, std::sin (twoPi * (float) i / 16.0f) });

            return p;
        }
    }
}

float evaluatePoints (const ShapePoints& pts, float x)
{
    const auto n = pts.size();

    if (n == 0)
        return 0.0f;

    if (n == 1)
        return pts[0].y;

    if (x < pts.front().x || x >= pts.back().x)
    {
        // Wrap-around segment from the last point to the first
        const auto a = pts.back(), b = pts.front();
        const float span = (b.x + 1.0f) - a.x;
        const float xx = x < pts.front().x ? x + 1.0f : x;
        return span <= 1.0e-6f ? b.y : a.y + (b.y - a.y) * (xx - a.x) / span;
    }

    for (size_t i = 0; i + 1 < n; ++i)
    {
        const auto a = pts[i], b = pts[i + 1];

        if (x >= a.x && x < b.x)
            return b.x - a.x <= 1.0e-6f ? b.y : a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
    }

    return pts.front().y;
}

juce::String pointsToString (const ShapePoints& pts)
{
    juce::StringArray parts;

    for (auto p : pts)
        parts.add (juce::String (p.x, 4) + "," + juce::String (p.y, 4));

    return parts.joinIntoString (";");
}

ShapePoints pointsFromString (const juce::String& text)
{
    ShapePoints pts;

    for (const auto& part : juce::StringArray::fromTokens (text, ";", {}))
    {
        const auto xy = juce::StringArray::fromTokens (part, ",", {});

        if (xy.size() == 2)
            pts.push_back ({ juce::jlimit (0.0f, 0.999f, xy[0].getFloatValue()), juce::jlimit (-1.0f, 1.0f, xy[1].getFloatValue()) });
    }

    sortPoints (pts);
    return pts.size() >= 2 ? pts : defaultCustomPoints();
}

float lfoShapeValue (int shape, float ph, const ShapePoints* customPoints, bool preview,
                     float hold, float prev, float next, float rawPhase)
{
    switch (shape)
    {
        case sine:     return std::sin (twoPi * ph);
        case triangle: return 1.0f - 4.0f * std::abs (ph - 0.5f);
        case sawUp:    return 2.0f * ph - 1.0f;
        case sawDown:  return 1.0f - 2.0f * ph;
        case square:   return ph < 0.5f ? 1.0f : -1.0f;

        case sampleHold:
            return preview ? previewRandom[juce::jmin (3, (int) (ph * 4.0f))] : hold;

        case smoothRandom:
        {
            if (preview)
            {
                const float q = ph * 4.0f;
                const int i = juce::jmin (3, (int) q);
                const float t = (1.0f - std::cos (juce::MathConstants<float>::pi * (q - (float) i))) * 0.5f;
                return previewRandom[i] + (previewRandom[(i + 1) % 4] - previewRandom[i]) * t;
            }

            const float t = (1.0f - std::cos (juce::MathConstants<float>::pi * rawPhase)) * 0.5f;
            return prev + (next - prev) * t;
        }

        case custom:
        default:
            return customPoints != nullptr ? evaluatePoints (*customPoints, ph) : 0.0f;
    }
}

//==============================================================================
void CustomShapeTable::setPoints (const ShapePoints& sortedPoints)
{
    for (int i = 0; i < size; ++i)
        table[(size_t) i].store (evaluatePoints (sortedPoints, (float) i / (float) size), std::memory_order_relaxed);
}

float CustomShapeTable::lookup (float phase) const noexcept
{
    const float pos = juce::jlimit (0.0f, 0.99999f, phase) * (float) size;
    const int i0 = (int) pos;
    const int i1 = (i0 + 1) % size;
    const float t = pos - (float) i0;
    const float a = table[(size_t) i0].load (std::memory_order_relaxed);
    const float b = table[(size_t) i1].load (std::memory_order_relaxed);
    return a + (b - a) * t;
}

//==============================================================================
void Lfo::newRandoms() noexcept
{
    prev = next;
    next = random.nextFloat() * 2.0f - 1.0f;
    hold = random.nextFloat() * 2.0f - 1.0f;
}

void Lfo::retrigger() noexcept
{
    phase = 0.0;
    newRandoms();
}

float Lfo::process (const Settings& s, int numSamples, double sampleRate, const CustomShapeTable& table,
                    double secondsSinceNote, std::optional<double> hostCyclePosition)
{
    if (hostCyclePosition.has_value())
    {
        const double locked = *hostCyclePosition - std::floor (*hostCyclePosition);

        if (locked < phase)
            newRandoms();

        phase = locked;
    }

    const float ph = (float) std::fmod (phase + (double) s.phaseOffset, 1.0);
    float v = s.shape == custom ? table.lookup (ph)
                                : lfoShapeValue (s.shape, ph, nullptr, false, hold, prev, next, (float) phase);

    if (s.unipolar)
        v = (v + 1.0f) * 0.5f;

    const double dt = numSamples / sampleRate;
    const float tau = s.smooth * s.smooth * 0.5f;
    smoothed = tau > 0.0f ? smoothed + (v - smoothed) * (1.0f - std::exp ((float) -dt / tau)) : v;

    const float fade = s.fadeSeconds > 0.005f ? (float) juce::jlimit (0.0, 1.0, secondsSinceNote / s.fadeSeconds) : 1.0f;
    output = smoothed * fade;

    displayPhase  = ph;
    displayOutput = output;

    if (! hostCyclePosition.has_value())
    {
        phase += s.rateHz * dt;

        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            newRandoms();
        }
    }

    return output;
}

} // namespace sw

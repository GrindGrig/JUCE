// Headless check of the meter engine and generator: prints each genre in a few
// meters as ASCII step grids, and validates the MIDI export.
#include <juce_core/juce_core.h>
#include "Generator.h"
#include "MidiExport.h"

using namespace gf;

static void printPattern (const Pattern& p)
{
    const auto m = Meter::create (p.numerator, p.denominator, p.grouping);
    const int steps = m.stepsPerBar * p.bars;

    juce::String header ("            ");
    for (int s = 0; s < steps; ++s)
    {
        const auto& st = m.steps[(size_t) (s % m.stepsPerBar)];
        header << (st.barStart ? "|" : st.superStart ? "!" : st.tactusStart ? ":" : ".");
    }
    std::cout << header << "\n";

    for (int lane = 0; lane < NumLanes; ++lane)
    {
        juce::String row = juce::String (laneInfo (lane).name).paddedRight (' ', 12);
        juce::String cells = juce::String::repeatedString (" ", steps);
        bool any = false;
        for (auto& n : p.notes)
        {
            if (n.lane != lane) continue;
            const double stepPos = n.beat * 4.0;
            const int idx = juce::jlimit (0, steps - 1, (int) std::floor (stepPos + 0.5));
            const bool offGrid = std::abs (stepPos - std::round (stepPos)) > 0.05;
            const juce::juce_wchar ch = offGrid ? '~' : (n.velocity > 0.7f ? 'X' : n.velocity > 0.4f ? 'x' : 'o');
            cells = cells.substring (0, idx) + juce::String::charToString (ch) + cells.substring (idx + 1);
            any = true;
        }
        if (any) std::cout << row << cells << "\n";
    }
}

int main()
{
    const struct { int num, den; } meters[] = { { 4, 4 }, { 7, 8 }, { 6, 8 }, { 5, 4 }, { 3, 4 }, { 11, 8 } };

    for (auto meterSpec : meters)
    {
        auto m = Meter::create (meterSpec.num, meterSpec.den);
        std::cout << "\n===== " << meterSpec.num << "/" << meterSpec.den << "  grouping " << m.grouping
                  << "  tactus=" << m.numTactus() << " compound=" << (int) m.compound
                  << " backbeats:";
        for (auto b : m.backbeatSteps) std::cout << " " << b;
        std::cout << " halftime=" << m.halfTimeStep << "\n";
    }

    std::array<bool, NumLanes> all; all.fill (true);

    for (int g = 0; g < (int) Genre::NumGenres; ++g)
    {
        for (auto meterSpec : { std::pair<int,int> { 4, 4 }, std::pair<int,int> { 7, 8 } })
        {
            Pattern p;
            p.numerator = meterSpec.first;
            p.denominator = meterSpec.second;
            p.bars = 2;
            GeneratorParams params;
            params.genre = (Genre) g;
            params.complexity = 0.55f;
            params.humanize = 0.0f;
            params.swing = 0.5f;
            params.seed = 42;
            Generator::generate (p, params, all);
            std::cout << "\n--- " << Generator::info ((Genre) g).name << " in " << p.numerator << "/" << p.denominator
                      << " (" << p.grouping << "), " << p.notes.size() << " notes\n";
            printPattern (p);

            for (auto& n : p.notes)
                if (n.beat < 0 || n.beat >= p.lengthInBeats())
                {
                    std::cout << "ERROR: note outside pattern\n";
                    return 1;
                }
        }
    }

    // MIDI export round-trip
    Pattern p; p.numerator = 7; p.denominator = 8; p.bars = 2;
    GeneratorParams params; params.genre = Genre::Rock;
    Generator::generate (p, params, all);
    std::array<LaneSettings, NumLanes> lanes;
    for (int l = 0; l < NumLanes; ++l) { lanes[(size_t) l].midiNote = laneInfo (l).defaultNote; lanes[(size_t) l].altMidiNote = laneInfo (l).defaultAltNote; }

    auto f = juce::File::createTempFile (".mid");
    if (! MidiExport::writeToFile (f, p, lanes, 120.0, true)) { std::cout << "ERROR writing midi\n"; return 1; }

    juce::FileInputStream in (f);
    juce::MidiFile mf;
    if (! mf.readFrom (in)) { std::cout << "ERROR reading midi\n"; return 1; }
    int noteOns = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (auto* e : *mf.getTrack (t))
            if (e->message.isNoteOn()) ++noteOns;
    std::cout << "\nMIDI export: tracks=" << mf.getNumTracks() << " noteOns=" << noteOns << " (pattern notes " << p.notes.size() << ")\n";
    f.deleteFile();
    return noteOns == (int) p.notes.size() ? 0 : 1;
}

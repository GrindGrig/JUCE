// Offline test: runs the processor with a fake host playhead and checks that the
// MIDI it emits matches the pattern (loop wrap, timing, note-offs) and that audio is produced.
#include "PluginProcessor.h"

using namespace gf;

struct FakePlayHead final : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setPpqPosition (ppq);
        p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { 7, 8 });
        return p;
    }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    GrooveForgeProcessor proc;
    proc.setTimeSignature (7, 8, "2+2+3");
    proc.getParams().genre = Genre::Rock;
    proc.generate (-1, false);

    const double sr = 48000.0;
    const int block = 512;
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);

    FakePlayHead ph;
    proc.setPlayHead (&ph);

    const auto& pat = proc.getPattern();
    const double len = pat.lengthInBeats();
    std::cout << "pattern " << pat.numerator << "/" << pat.denominator << " len " << len << " beats, notes " << pat.notes.size() << "\n";

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    int ons = 0, offs = 0;
    double peak = 0.0;
    std::vector<double> onBeats;

    const double totalBeats = len * 2.0;      // two loops
    while (ph.ppq < totalBeats)
    {
        midi.clear();
        proc.processBlock (buffer, midi);
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOn())
            {
                const double b = ph.ppq + m.samplePosition * ph.bpm / 60.0 / sr;
                if (b < totalBeats - 1.0e-9) ++ons;   // the last block may run into a 3rd loop
                onBeats.push_back (b);
            }
            if (msg.isNoteOff()) ++offs;
        }
        peak = std::max (peak, (double) buffer.getMagnitude (0, block));
        ph.ppq += block * ph.bpm / 60.0 / sr;
    }

    ph.playing = false;
    midi.clear();
    proc.processBlock (buffer, midi);
    for (const auto m : midi) if (m.getMessage().isNoteOff()) ++offs;

    // every pattern note should appear exactly twice (allowing a note at the very end of the window)
    int expected = 0;
    for (auto& n : pat.notes) if (! proc.getLanes()[(size_t) n.lane].muted) expected += 2;

    // timing check: each emitted note-on is within one sample of a pattern note
    double worst = 0.0;
    for (auto b : onBeats)
    {
        const double inLoop = std::fmod (b, len);
        double best = 1e9;
        for (auto& n : pat.notes) best = std::min ({ best, std::abs (n.beat - inLoop), std::abs (n.beat + len - inLoop), std::abs (n.beat - len - inLoop) });
        worst = std::max (worst, best);
    }

    const double oneSampleBeats = ph.bpm / 60.0 / sr;
    std::cout << "note-ons " << ons << " (expected " << expected << "), note-offs " << offs
              << ", worst timing error " << worst / oneSampleBeats << " samples, audio peak " << peak << "\n";

    const bool ok = ons == expected && offs == (int) onBeats.size() && worst <= oneSampleBeats * 1.01 && peak > 0.01;
    std::cout << (ok ? "PASS" : "FAIL") << "\n";
    return ok ? 0 : 1;
}

#include "PluginEditor.h"

namespace
{
    namespace C = cicada::colours;

    constexpr int windowW = 900, windowH = 584, margin = 12, gap = 8;
    constexpr int cellW = 66, cellH = 74;

    juce::String noteName (float midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int m = juce::roundToInt (midi);
        return juce::String (names[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
    }

    const juce::StringArray modeHelpText {
        "Tracks A's pitch and loudness, then rebuilds every note from B's harmonic recipe at that loudness. Cleanest for melodic material.",
        "Rebuilds A from short fragments of the B recording, matched by loudness, brightness and pitch. Keeps B's real texture.",
        "Imprints A's changing spectral shape onto the B recording. B keeps its own notes; A supplies rhythm and articulation."
    };

    void setupStatus (juce::Label& l)
    {
        l.setFont (ui::monoFont());
        l.setColour (juce::Label::textColourId, C::sage);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setMinimumHorizontalScale (1.0f);
        l.setBorderSize ({});
    }

    void setupPill (juce::ToggleButton& b, juce::Colour accent)
    {
        b.setColour (juce::ToggleButton::tickColourId, accent);
        b.setClickingTogglesState (false);
    }

    void setupTextButton (juce::TextButton& b, juce::Colour accent, const juce::String& tip)
    {
        b.setColour (juce::TextButton::buttonOnColourId, accent);
        b.setColour (juce::TextButton::textColourOffId, C::sage);
        b.setColour (juce::TextButton::textColourOnId, C::ink);
        b.setTooltip (tip);
    }

    /** Grid lines for octaves C2, C4, C6 with labels in the left gutter. */
    void paintPitchGrid (juce::Graphics& g, juce::Rectangle<float> r, float gutter, std::function<float (float)> yOf)
    {
        g.setFont (ui::monoFont().withHeight (9.5f));
        for (int m = 36; m <= 84; m += 24)
        {
            const float y = yOf ((float) m);
            g.setColour (C::line);
            g.fillRect (r.getX() + gutter, y, r.getWidth() - gutter, 1.0f);
            g.setColour (C::sage);
            g.drawText ("C" + juce::String (m / 12 - 1), juce::Rectangle<float> (r.getX() + 3.0f, y - 6.0f, gutter, 12.0f), juce::Justification::centredLeft);
        }
    }
}

namespace ui
{
juce::Font labelFont() { return juce::FontOptions (9.5f, juce::Font::bold).withKerningFactor (0.1f); }
juce::Font monoFont()  { return juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain); }

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& caption, juce::Colour accent)
    : attachment (state, id, slider)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::textBoxTextColourId, C::ink);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, cellW, 14);
    auto* p = state.getParameter (id);
    slider.setDoubleClickReturnValue (true, (double) state.getParameterRange (id).convertFrom0to1 (p->getDefaultValue()));
    slider.setTooltip (p->getName (64) + ". Drag up or down; double-click to reset.");

    label.setText (caption.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (labelFont());
    label.setColour (juce::Label::textColourId, C::sage);
    label.setInterceptsMouseClicks (false, false);
    label.setBorderSize ({});

    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (13));
    slider.setBounds (r);
}

Cell::Cell (const juce::String& caption, juce::Component& c, int h) : content (c), contentHeight (h)
{
    label.setText (caption.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (labelFont());
    label.setColour (juce::Label::textColourId, C::sage);
    label.setBorderSize ({});
    addAndMakeVisible (label);
    addAndMakeVisible (content);
}

void Cell::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (13));
    content.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, contentHeight).translated (0, -6));
}

//==============================================================================
void InputView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (C::well);
    g.fillRoundedRectangle (r, 7.0f);

    constexpr float gutter = 24.0f, lo = 28.0f, hi = 96.0f;
    const auto yOf = [&] (float m) { return r.getBottom() - 6.0f - (juce::jlimit (lo, hi, m) - lo) / (hi - lo) * (r.getHeight() - 12.0f); };
    paintPitchGrid (g, r, gutter, yOf);

    constexpr int size = (int) std::tuple_size<decltype (engine.telemetry.items)>::value;
    const int written = engine.telemetry.written.load (std::memory_order_acquire);
    const int count = juce::jmin (written, size);
    const float plotW = r.getWidth() - gutter - 4.0f, x0 = r.getX() + gutter;
    const auto xOf = [&] (int i) { return x0 + plotW * (float) (size - count + i) / (float) (size - 1); };
    const auto item = [&] (int i) { return engine.telemetry.items[(size_t) ((written - count + i) % size)]; };

    // loudness, as a soft filled envelope around the centre line
    juce::Path level;
    const float mid = r.getCentreY(), half = r.getHeight() * 0.42f;
    for (int i = 0; i < count; ++i)
    {
        const float a = juce::jlimit (0.0f, 1.0f, (item (i).loudnessDb + 70.0f) / 70.0f);
        i == 0 ? level.startNewSubPath (xOf (i), mid - a * half) : level.lineTo (xOf (i), mid - a * half);
    }
    for (int i = count - 1; i >= 0; --i)
    {
        const float a = juce::jlimit (0.0f, 1.0f, (item (i).loudnessDb + 70.0f) / 70.0f);
        level.lineTo (xOf (i), mid + a * half);
    }
    if (count > 1)
    {
        level.closeSubPath();
        g.setColour (ui::roles::performance.withAlpha (0.2f));
        g.fillPath (level);
    }

    const auto line = [&] (bool played, juce::Colour colour, float width)
    {
        juce::Path p;
        bool pen = false;
        for (int i = 0; i < count; ++i)
        {
            const float m = played ? item (i).outMidi : item (i).midi;
            if (m <= 0.0f) { pen = false; continue; }
            pen ? p.lineTo (xOf (i), yOf (m)) : p.startNewSubPath (xOf (i), yOf (m));
            pen = true;
        }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    line (true, ui::roles::transfer.withAlpha (0.85f), 1.2f);
    line (false, ui::roles::performance, 1.6f);

    if (written == 0)
    {
        g.setColour (C::sage);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Waiting for audio on the plug-in's input", r, juce::Justification::centred);
    }
}

//==============================================================================
void TimbreView::setModel (const tt::TimbreModel* m)
{
    model = m;
    peaks.clear();
    if (model != nullptr)
    {
        constexpr int columns = 420;
        const auto& a = model->audio;
        const size_t per = juce::jmax ((size_t) 1, a.size() / columns);
        for (size_t c = 0; c < columns && c * per < a.size(); ++c)
        {
            float mn = 0.0f, mx = 0.0f;
            for (size_t i = c * per; i < juce::jmin (a.size(), (c + 1) * per); ++i) { mn = juce::jmin (mn, a[i]); mx = juce::jmax (mx, a[i]); }
            peaks.emplace_back (mn, mx);
        }
    }
    repaint();
}

void TimbreView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (C::well);
    g.fillRoundedRectangle (r, 7.0f);
    if (model == nullptr) return;

    constexpr float gutter = 24.0f, lo = 28.0f, hi = 96.0f;
    const auto yOf = [&] (float m) { return r.getBottom() - 5.0f - (juce::jlimit (lo, hi, m) - lo) / (hi - lo) * (r.getHeight() - 10.0f); };
    paintPitchGrid (g, r, gutter, yOf);

    const float x0 = r.getX() + gutter, w = r.getWidth() - gutter - 4.0f;
    g.setColour (ui::roles::timbre.withAlpha (0.22f));
    for (size_t c = 0; c < peaks.size(); ++c)
    {
        const float x = x0 + w * (float) c / (float) peaks.size();
        const float y0 = r.getCentreY() - peaks[c].second * r.getHeight() * 0.46f, y1 = r.getCentreY() - peaks[c].first * r.getHeight() * 0.46f;
        g.fillRect (x, y0, juce::jmax (1.0f, w / (float) peaks.size()), juce::jmax (1.0f, y1 - y0));
    }

    juce::Path p;
    bool pen = false;
    const int nF = model->numFrames();
    for (int i = 0; i < nF; ++i)
    {
        if (! model->voiced[(size_t) i]) { pen = false; continue; }
        const float x = x0 + w * (float) i / (float) juce::jmax (1, nF - 1), y = yOf (model->midi[(size_t) i]);
        pen ? p.lineTo (x, y) : p.startNewSubPath (x, y);
        pen = true;
    }
    g.setColour (ui::roles::timbre);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
void FingerprintView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (C::well);
    g.fillRoundedRectangle (r, 7.0f);
    if (model == nullptr) return;

    constexpr float L = 24.0f, B = 13.0f;
    const auto xOf = [&] (float f) { return r.getX() + L + std::log (f / tt::gridLowHz) / std::log (tt::gridHighHz / tt::gridLowHz) * (r.getWidth() - L - 6.0f); };
    const auto yOf = [&] (float d) { return r.getY() + 5.0f + (-juce::jlimit (-64.0f, 2.0f, d)) / 60.0f * (r.getHeight() - B - 5.0f); };

    g.setFont (ui::monoFont().withHeight (9.5f));
    for (float d : { 0.0f, -30.0f, -60.0f })
    {
        g.setColour (C::line);
        g.fillRect (r.getX() + L, yOf (d), r.getWidth() - L, 1.0f);
        g.setColour (C::sage);
        g.drawText (juce::String ((int) d), juce::Rectangle<float> (r.getX() + 2.0f, yOf (d) - 6.0f, L, 12.0f), juce::Justification::centredLeft);
    }
    for (auto [f, t] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k Hz" } })
    {
        const float x = xOf (f);
        g.setColour (C::line);
        g.fillRect (x, r.getY() + 5.0f, 1.0f, r.getHeight() - B - 5.0f);
        g.setColour (C::sage);
        g.drawText (t, juce::Rectangle<float> (juce::jlimit (r.getX() + L, r.getRight() - 46.0f, x - 23.0f), r.getBottom() - B, 46.0f, B), juce::Justification::centred);
    }

    g.saveState();
    g.reduceClipRegion (juce::Rectangle<float> (r.getX() + L, r.getY(), r.getWidth() - L, r.getHeight() - B).toNearestInt());
    const auto curve = [&] (const tt::Envelope& e, float offset, juce::Colour c, bool dashed)
    {
        juce::Path p;
        for (int i = 0; i < tt::gridSize; ++i)
        {
            const float x = xOf (tt::gridFrequency (i)), y = yOf (e[(size_t) i] + offset);
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        g.setColour (c);
        if (dashed)
        {
            juce::Path d;
            const float dash[] = { 3.0f, 2.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (d, p, dash, 2);
            g.fillPath (d);
        }
        else g.strokePath (p, juce::PathStrokeType (1.6f));
    };
    curve (model->tiers[1].noise, -26.0f, C::sage, true);
    curve (model->tiers[0].harmonic, 0.0f, ui::roles::timbre.withAlpha (0.3f), false);
    curve (model->tiers[1].harmonic, 0.0f, ui::roles::timbre.withAlpha (0.62f), false);
    curve (model->tiers[2].harmonic, 0.0f, ui::roles::timbre, false);
    g.restoreState();
}

//==============================================================================
SpectrogramView::SpectrogramView (tt::TransferEngine& e) : engine (e)
{
    re.assign (1024, 0.0); im.assign (1024, 0.0); window.assign (1024, 0.0);
    for (int i = 0; i < 1024; ++i) window[(size_t) i] = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / 1024);
    lastRead = engine.outputScope.written.load();
}

void SpectrogramView::resized()
{
    image = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
    juce::Graphics g (image);
    g.fillAll (cicada::spectrogramColour (0.0f));
}

void SpectrogramView::update()
{
    if (! image.isValid()) return;

    constexpr int ringSize = (int) std::tuple_size<decltype (engine.outputScope.items)>::value;
    const int written = engine.outputScope.written.load (std::memory_order_acquire);
    int fresh = written - lastRead;
    if (fresh < 0 || fresh > ringSize) { fresh = juce::jmin (ringSize, juce::jmax (0, written)); }
    for (int i = written - fresh; i < written; ++i)
        pending.push_back (engine.outputScope.items[(size_t) (i % ringSize)]);
    lastRead = written;

    const double sr = engine.getSampleRate();
    const int hopUi = juce::jmax (256, (int) (sr / 86.0));   // about 86 columns a second
    const int H = image.getHeight(), W = image.getWidth();
    int columns = 0;

    while ((int) pending.size() >= 1024)
    {
        for (int i = 0; i < 1024; ++i) { re[(size_t) i] = pending[(size_t) i] * window[(size_t) i]; im[(size_t) i] = 0.0; }
        fft.perform (re.data(), im.data(), false);

        image.moveImageSection (0, 0, 1, 0, W - 1, H);
        for (int y = 0; y < H; ++y)
        {
            const double f = 50.0 * std::pow (10000.0 / 50.0, 1.0 - (double) y / juce::jmax (1, H - 1));
            const double b = f * 1024.0 / sr;
            const int i0 = juce::jlimit (0, 511, (int) b);
            const double t = b - i0;
            const auto mag = [&] (int k) { return std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]); };
            const double m = mag (i0) * (1.0 - t) + mag (juce::jmin (512, i0 + 1)) * t;
            const double db = 20.0 * std::log10 (m * 4.0 / 1024.0 + 1.0e-9);
            const float v = std::pow ((float) juce::jlimit (0.0, 1.0, (db + 90.0) / 78.0), 1.7f);
            image.setPixelAt (W - 1, y, cicada::spectrogramColour (v));
        }

        pending.erase (pending.begin(), pending.begin() + hopUi);
        ++columns;
    }

    if (columns > 0) repaint();
}

void SpectrogramView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    juce::Path clip;
    clip.addRoundedRectangle (r, 7.0f);
    g.saveState();
    g.reduceClipRegion (clip);
    g.drawImageAt (image, 0, 0);
    g.restoreState();
}
} // namespace ui

//==============================================================================
TimbalEditor::TimbalEditor (TimbalProcessor& p)
    : AudioProcessorEditor (p), processor (p), inputView (p.engine), spectrogram (p.engine)
{
    setLookAndFeel (&lnf);
    auto& state = processor.parameters;
    using namespace ui::roles;

    using Slot = TimbalProcessor::Slot;

    // header: who plays and who gives the colour
    setupPill (swapToggle, C::ink);
    swapToggle.setClickingTogglesState (true);
    swapToggle.setTooltip ("Swap the roles: the plug-in's input becomes the timbre (B), and A plays from a clip in time with your host");
    swapAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ids::swap, swapToggle);
    addAndMakeVisible (swapToggle);

    // A
    addAndMakeVisible (inputView);
    setupStatus (inputStatus);
    addAndMakeVisible (inputStatus);
    const auto performances = TimbalProcessor::exampleNames (Slot::performanceClip);
    for (int i = 0; i < performances.size(); ++i)
    {
        auto* b = performancePills.add (new juce::ToggleButton (performances[i].toUpperCase()));
        setupPill (*b, performance);
        b->setTooltip ("Play the built-in " + performances[i].toLowerCase() + " as the performance");
        b->onClick = [this, i] { processor.requestClip (Slot::performanceClip, { i, {} }); };
        addChildComponent (b);
    }
    setupTextButton (loadA, performance, "Load an audio file to perform (or drop one on this panel)");
    loadA.onClick = [this] { browseFor (Slot::performanceClip); };
    setupTextButton (resetA, performance, "Go back to the built-in hummed melody");
    resetA.onClick = [this] { flash (panelA); processor.requestClip (Slot::performanceClip, { 0, {} }); };
    addChildComponent (loadA);
    addChildComponent (resetA);

    // B
    const auto names = tt::builtInTimbreNames();
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = presetPills.add (new juce::ToggleButton (names[i].toUpperCase()));
        setupPill (*b, timbre);
        b->setTooltip ("Use the built-in " + names[i].toLowerCase() + " example as the timbre");
        b->onClick = [this, i] { processor.requestClip (Slot::timbreClip, { i, {} }); };
        addAndMakeVisible (b);
    }
    setupTextButton (loadButton, timbre, "Load an audio file as the timbre (or drop one on this panel)");
    loadButton.onClick = [this] { browseFor (Slot::timbreClip); };
    setupTextButton (resetB, timbre, "Go back to the built-in cello");
    resetB.onClick = [this] { flash (panelB); processor.requestClip (Slot::timbreClip, { 0, {} }); };
    for (auto* c : std::initializer_list<juce::Component*> { &loadButton, &resetB, &timbreView, &fingerprint }) addAndMakeVisible (c);
    setupStatus (timbreStatus);
    addAndMakeVisible (timbreStatus);

    // Transfer
    const auto modes = TimbalProcessor::modeNames();
    for (int i = 0; i < modes.size(); ++i)
    {
        auto* b = modePills.add (new juce::ToggleButton (modes[i].toUpperCase()));
        setupPill (*b, transfer);
        b->setTooltip (modeHelpText[i]);
        b->onClick = [this, i]
        {
            if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.parameters.getParameter (ids::mode)))
            {
                param->beginChangeGesture();
                *param = i;
                param->endChangeGesture();
            }
            refreshMode();
        };
        addAndMakeVisible (b);
    }
    setupTextButton (resetT, transfer, "Reset the method and every transfer control");
    resetT.onClick = [this] { flash (panelT); processor.resetParameters (ids::transferSection()); refreshMode(); };
    addAndMakeVisible (resetT);

    modeHelp.setFont (juce::FontOptions (11.5f));
    modeHelp.setColour (juce::Label::textColourId, C::sage);
    modeHelp.setBorderSize ({});
    modeHelp.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (modeHelp);

    setupPill (octaveToggle, transfer);
    octaveToggle.setClickingTogglesState (true);
    octaveToggle.setTooltip ("Move A into B's octave, so a hummed line lands in a cello's range");
    octaveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ids::autoOctave, octaveToggle);
    octaveCell = std::make_unique<ui::Cell> ("Octave", octaveToggle);

    grainBox.addItemList ({ "Short", "Long" }, 1);
    grainBox.setTooltip ("Short grains keep timing tight; long grains keep B's texture smoother");
    grainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::grain, grainBox);
    grainCell = std::make_unique<ui::Cell> ("Grain", grainBox);

    const auto knob = [&] (const juce::String& id, const juce::String& caption, juce::Colour c) { return std::make_unique<ui::Knob> (state, id, caption, c); };
    transpose  = knob (ids::transpose, "Transpose", transfer);
    snap       = knob (ids::snap, "Snap", transfer);
    brightness = knob (ids::brightness, "Brightness", transfer);
    noise      = knob (ids::noise, "Noise", transfer);
    consonants = knob (ids::consonants, "Consonants", transfer);
    vibrato    = knob (ids::vibrato, "Vibrato", transfer);
    glide      = knob (ids::glide, "Glide", transfer);
    continuity = knob (ids::continuity, "Continuity", transfer);
    pitchFix   = knob (ids::pitchFix, "Pitch fix", transfer);
    detail     = knob (ids::detail, "Detail", transfer);
    flatten    = knob (ids::flatten, "Flatten", transfer);
    air        = knob (ids::air, "Air", transfer);

    constexpr int H = 1, M = 2, V = 4;
    transferSlots = { { transpose.get(), H | M }, { octaveCell.get(), H | M }, { snap.get(), H }, { brightness.get(), H }, { noise.get(), H },
                      { consonants.get(), H }, { vibrato.get(), H }, { glide.get(), H }, { grainCell.get(), M }, { continuity.get(), M },
                      { pitchFix.get(), M }, { detail.get(), V }, { flatten.get(), V }, { air.get(), V } };
    for (auto& s : transferSlots) addChildComponent (s.c);

    // Output
    gate = knob (ids::gate, "Gate", output);
    mix  = knob (ids::mix, "Mix", output);
    gain = knob (ids::gain, "Gain", output);
    for (auto* c : std::initializer_list<juce::Component*> { &spectrogram, gate.get(), mix.get(), gain.get(), &resetO }) addAndMakeVisible (c);
    setupStatus (outputStatus);
    addAndMakeVisible (outputStatus);
    setupTextButton (resetO, output, "Reset gate, mix and gain");
    resetO.onClick = [this] { flash (panelO); processor.resetParameters (ids::outputSection()); };

    setupTextButton (resetAll, C::eye, "Reset every control and go back to the built-in examples (Swap A / B stays as it is)");
    resetAll.setColour (juce::TextButton::textColourOffId, C::ink);
    resetAll.onClick = [this]
    {
        processor.resetParameters (ids::transferSection());
        processor.resetParameters (ids::outputSection());
        for (auto slot : { Slot::timbreClip, Slot::performanceClip })
            if (! (processor.getSource (slot) == TimbalProcessor::ClipSource {})) processor.requestClip (slot, { 0, {} });
        flash (getLocalBounds());
        refreshMode();
    };
    addAndMakeVisible (resetAll);

    processor.addChangeListener (this);
    setSize (windowW, windowH);
    applySwapLayout();
    refreshMode();
    startTimerHz (30);
}

TimbalEditor::~TimbalEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void TimbalEditor::paint (juce::Graphics& g)
{
    cicada::paintVenation (g, getLocalBounds());
    cicada::paintHeader (g, getLocalBounds().removeFromTop (40).reduced (margin, 0), "TiMBAL", "play one sound through the character of another", true);

    const auto panel = [&] (juce::Rectangle<int> r, const juce::String& title, juce::Colour accent, bool highlighted)
    {
        cicada::paintMembrane (g, r.toFloat(), highlighted);
        cicada::paintTitle (g, r.reduced (10, 5).removeFromTop (18), title.toUpperCase(), accent);
    };
    const bool swapped = processor.isSwapped();
    const auto dot = "  " + juce::String::charToString (0x00b7) + "  ";
    panel (panelA, "A / Performance" + dot + (swapped ? "clip" : "input"), ui::roles::performance, dragOver && swapped);
    panel (panelB, "B / Timbre" + dot + (swapped ? "input" : "clip"), ui::roles::timbre, dragOver && ! swapped);
    panel (panelT, "Transfer", ui::roles::transfer, false);
    panel (panelO, "Output", ui::roles::output, false);

    // spectrogram axis, outside the image on the left
    const auto spec = spectrogram.getBounds();
    g.setFont (ui::monoFont());
    g.setColour (C::sage);
    for (auto [f, t] : { std::pair<float, const char*> { 10000.0f, "10k" }, { 1000.0f, "1k" }, { 100.0f, "100" } })
    {
        const float frac = juce::jlimit (0.06f, 0.94f, 1.0f - std::log (f / 50.0f) / std::log (200.0f));
        const int y = spec.getY() + juce::roundToInt (frac * (float) spec.getHeight());
        g.drawText (t, juce::Rectangle<int> (spec.getX() - 30, y - 7, 26, 14), juce::Justification::centredRight);
    }

    // a short border flash after a Reset
    if (flashAlpha > 0.0f)
    {
        g.setColour (C::vein.withAlpha (flashAlpha));
        g.drawRoundedRectangle (flashArea.toFloat().reduced (0.5f), 10.0f, 1.6f);
    }
}

void TimbalEditor::resized()
{
    auto area = getLocalBounds().reduced (margin, 0);
    resetAll.setBounds (getWidth() - margin - 84, 9, 84, 22);
    swapToggle.setBounds (resetAll.getX() - 8 - 104, 9, 104, 22);
    const bool swapped = processor.isSwapped();
    const auto layoutPills = [] (juce::OwnedArray<juce::ToggleButton>& pills, juce::Rectangle<int> row)
    {
        for (auto* b : pills)
        {
            const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (ui::labelFont().withHeight (10.0f), b->getButtonText())) + 26;
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (4);
        }
    };
    area.removeFromTop (40 + gap);

    auto top = area.removeFromTop (204);
    panelA = top.removeFromLeft ((top.getWidth() - gap) / 2);
    top.removeFromLeft (gap);
    panelB = top;
    area.removeFromTop (gap);
    panelT = area.removeFromTop (138);
    area.removeFromTop (gap);
    panelO = area.withTrimmedBottom (margin);

    // A
    {
        auto r = panelA.reduced (10, 8);
        auto head = r.removeFromTop (22).withTrimmedLeft (230);
        resetA.setBounds (head.removeFromRight (54));
        head.removeFromRight (6);
        loadA.setBounds (head.removeFromRight (76));
        if (swapped)
        {
            r.removeFromTop (4);
            layoutPills (performancePills, r.removeFromTop (22));
            r.removeFromTop (6);
        }
        inputStatus.setBounds (r.removeFromBottom (14));
        r.removeFromBottom (4);
        inputView.setBounds (r);
    }

    // B
    {
        auto r = panelB.reduced (10, 8);
        auto head = r.removeFromTop (22).withTrimmedLeft (200);
        resetB.setBounds (head.removeFromRight (54));
        head.removeFromRight (6);
        loadButton.setBounds (head.removeFromRight (76));
        r.removeFromTop (4);
        if (! swapped)
        {
            layoutPills (presetPills, r.removeFromTop (22));
            r.removeFromTop (6);
        }
        timbreStatus.setBounds (r.removeFromBottom (14));
        r.removeFromBottom (4);
        const int h = (r.getHeight() - 4) / 2;
        timbreView.setBounds (r.removeFromTop (h));
        r.removeFromTop (4);
        fingerprint.setBounds (r);
    }

    // Transfer
    {
        auto r = panelT.reduced (10, 8);
        auto head = r.removeFromTop (22);
        resetT.setBounds (head.removeFromRight (54));
        head.removeFromLeft (92);
        for (auto* b : modePills)
        {
            const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (ui::labelFont().withHeight (10.0f), b->getButtonText())) + 28;
            b->setBounds (head.removeFromLeft (w));
            head.removeFromLeft (4);
        }
        r.removeFromTop (4);
        modeHelp.setBounds (r.removeFromTop (17));
        layoutTransferControls();
    }

    // Output
    {
        auto r = panelO.reduced (10, 8);
        auto head = r.removeFromTop (22);
        resetO.setBounds (head.removeFromRight (54));
        head.removeFromLeft (84);
        outputStatus.setBounds (head.withTrimmedRight (8));
        r.removeFromTop (4);
        auto knobs = r.removeFromRight (3 * cellW + 8);
        for (auto* k : { gate.get(), mix.get(), gain.get() })
        {
            k->setBounds (knobs.removeFromLeft (cellW).withSizeKeepingCentre (cellW, cellH));
            knobs.removeFromLeft (4);
        }
        r.removeFromRight (8);
        r.removeFromLeft (30);
        spectrogram.setBounds (r);
    }
}

void TimbalEditor::layoutTransferControls()
{
    auto r = panelT.reduced (10, 8);
    r.removeFromTop (22 + 4 + 17 + 4);
    const int mode = juce::jlimit (0, 2, (int) processor.parameters.getRawParameterValue (ids::mode)->load());
    int x = r.getX();
    for (auto& s : transferSlots)
    {
        const bool visible = (s.modes >> mode) & 1;
        s.c->setVisible (visible);
        if (! visible) continue;
        s.c->setBounds (x, r.getY(), cellW, cellH);
        x += cellW + 4;
    }
}

//==============================================================================
void TimbalEditor::refreshMode()
{
    const int mode = juce::jlimit (0, 2, (int) processor.parameters.getRawParameterValue (ids::mode)->load());
    for (int i = 0; i < modePills.size(); ++i) modePills[i]->setToggleState (i == mode, juce::dontSendNotification);
    modeHelp.setText (modeHelpText[mode], juce::dontSendNotification);
    shownMode = mode;
    layoutTransferControls();
}

void TimbalEditor::applySwapLayout()
{
    const bool swapped = processor.isSwapped();
    shownSwap = swapped;
    for (auto* b : performancePills) b->setVisible (swapped);
    loadA.setVisible (swapped);
    resetA.setVisible (swapped);
    for (auto* b : presetPills) b->setVisible (! swapped);
    loadButton.setVisible (! swapped);
    resetB.setVisible (! swapped);
    resized();
    refreshSources();
    repaint();
}

juce::Rectangle<int> TimbalEditor::clipPanel() const { return processor.isSwapped() ? panelA : panelB; }

void TimbalEditor::refreshSources()
{
    using Slot = TimbalProcessor::Slot;
    const bool swapped = processor.isSwapped();

    const auto timbreSrc = processor.getSource (Slot::timbreClip), performanceSrc = processor.getSource (Slot::performanceClip);
    for (int i = 0; i < presetPills.size(); ++i)      presetPills[i]->setToggleState (timbreSrc.builtIn == i, juce::dontSendNotification);
    for (int i = 0; i < performancePills.size(); ++i) performancePills[i]->setToggleState (performanceSrc.builtIn == i, juce::dontSendNotification);

    const auto* model = processor.getActiveTimbre();
    if (model != shownModel)
    {
        shownModel = model;
        timbreView.setModel (model);
        fingerprint.setModel (model);
    }

    const auto describe = [] (const tt::TimbreModel& m)
    {
        return m.hasPitch ? " | range " + noteName (m.lowMidi) + "-" + noteName (m.highMidi) : juce::String (" | no steady pitch, using its average spectrum");
    };

    if (swapped)
    {
        if (model == nullptr)
        {
            timbreStatus.setColour (juce::Label::textColourId, ui::roles::transfer);
            timbreStatus.setText ("Listening: play audio into the plug-in to set the timbre", juce::dontSendNotification);
        }
        else
        {
            timbreStatus.setColour (juce::Label::textColourId, C::sage);
            const double age = juce::jmax (0.0, processor.secondsSinceLiveLearn());
            timbreStatus.setText ("Learning from the input | updated " + juce::String (age, 1) + " s ago" + describe (*model), juce::dontSendNotification);
        }
        return;
    }

    if (processor.isLoading (Slot::timbreClip))
    {
        timbreStatus.setColour (juce::Label::textColourId, ui::roles::transfer);
        timbreStatus.setText ("Analysing " + (timbreSrc.builtIn >= 0 ? tt::builtInTimbreNames()[timbreSrc.builtIn] : timbreSrc.file.getFileName()) + "...", juce::dontSendNotification);
    }
    else if (processor.getError (Slot::timbreClip).isNotEmpty())
    {
        timbreStatus.setColour (juce::Label::textColourId, C::eye);
        timbreStatus.setText (processor.getError (Slot::timbreClip), juce::dontSendNotification);
    }
    else if (model != nullptr)
    {
        timbreStatus.setColour (juce::Label::textColourId, C::sage);
        timbreStatus.setText (model->name + " | " + juce::String (model->durationSeconds(), 2) + " s" + describe (*model), juce::dontSendNotification);
    }
}

void TimbalEditor::changeListenerCallback (juce::ChangeBroadcaster*) { refreshSources(); }

void TimbalEditor::timerCallback()
{
    const int mode = juce::jlimit (0, 2, (int) processor.parameters.getRawParameterValue (ids::mode)->load());
    if (mode != shownMode) refreshMode();
    if (processor.isSwapped() != shownSwap) applySwapLayout();
    if (processor.getActiveTimbre() != shownModel || (shownSwap && ++statusTicks % 15 == 0)) refreshSources();

    inputView.repaint();
    spectrogram.update();

    // live readouts
    const int written = processor.engine.telemetry.written.load (std::memory_order_acquire);
    if (written > 0)
    {
        const auto t = processor.engine.telemetry.items[(size_t) ((written - 1) % (int) processor.engine.telemetry.items.size())];
        juce::String s;
        if (t.midi > 0.0f)
        {
            const int cents = juce::roundToInt ((t.midi - std::round (t.midi)) * 100.0f);
            s << noteName (t.midi) << (cents >= 0 ? " +" : " ") << cents << " ct";
        }
        else s << (t.loudnessDb > processor.parameters.getRawParameterValue (ids::gate)->load() ? "no pitch" : "below gate");
        s << " | " << juce::String (t.loudnessDb, 1) << " dB";
        inputStatus.setColour (juce::Label::textColourId, C::sage);
        inputStatus.setText (s, juce::dontSendNotification);
    }

    if (shownSwap)
    {
        // A is a clip: say which, and whether it is playing
        using Slot = TimbalProcessor::Slot;
        const auto* clip = processor.getModel (Slot::performanceClip);
        if (processor.isLoading (Slot::performanceClip))
            inputStatus.setText ("Loading the performance clip...", juce::dontSendNotification);
        else if (processor.getError (Slot::performanceClip).isNotEmpty())
        {
            inputStatus.setColour (juce::Label::textColourId, C::eye);
            inputStatus.setText (processor.getError (Slot::performanceClip), juce::dontSendNotification);
        }
        else if (clip != nullptr)
        {
            const float pos = processor.getPerformancePosition();
            inputStatus.setText (clip->name + " | " + (pos < 0.0f ? juce::String ("start playback in your host")
                                                                   : juce::String (pos * clip->durationSeconds(), 1) + " / " + juce::String (clip->durationSeconds(), 1) + " s")
                                 + (written > 0 && pos >= 0.0f ? " | " + inputStatus.getText().upToFirstOccurrenceOf (" |", false, false) : juce::String()),
                                 juce::dontSendNotification);
        }
    }

    const int shift = processor.engine.appliedShift.load();
    const double latencyMs = 1000.0 * processor.engine.latencySamples() / processor.engine.getSampleRate();
    outputStatus.setText ((mode == 2 ? juce::String ("B keeps its own notes") : "shift " + juce::String (shift > 0 ? "+" : "") + juce::String (shift) + " st")
                          + " | latency " + juce::String (latencyMs, 1) + " ms", juce::dontSendNotification);

    if (flashAlpha > 0.0f)
    {
        flashAlpha = juce::jmax (0.0f, flashAlpha - 1.0f / 18.0f);   // about 0.6 s at 30 Hz
        repaint (flashArea.expanded (2));
    }
}

void TimbalEditor::flash (juce::Rectangle<int> r)
{
    flashArea = r;
    flashAlpha = 1.0f;
    repaint (r.expanded (2));
}

//==============================================================================
void TimbalEditor::browseFor (TimbalProcessor::Slot slot)
{
    const bool performance = slot == TimbalProcessor::Slot::performanceClip;
    chooser = std::make_unique<juce::FileChooser> (performance ? "Choose a clip to perform" : "Choose a timbre clip", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile()) processor.requestClip (slot, { -1, fc.getResult() });
    });
}

bool TimbalEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void TimbalEditor::fileDragEnter (const juce::StringArray&, int, int) { dragOver = true; repaint (clipPanel()); }
void TimbalEditor::fileDragExit (const juce::StringArray&)           { dragOver = false; repaint (clipPanel()); }

void TimbalEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint (clipPanel());
    // the file goes to whichever side is a clip: B normally, A when swapped
    const auto slot = processor.isSwapped() ? TimbalProcessor::Slot::performanceClip : TimbalProcessor::Slot::timbreClip;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) { processor.requestClip (slot, { -1, juce::File (f) }); break; }
}

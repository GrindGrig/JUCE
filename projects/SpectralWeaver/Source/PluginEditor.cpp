#include "PluginEditor.h"

namespace ui
{

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colours::accentFx.withAlpha (0.4f));
    setColour (juce::ComboBox::backgroundColourId, colours::background);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::outline);
    setColour (juce::ComboBox::arrowColourId, colours::dim);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accentFx.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour (juce::TextEditor::backgroundColourId, colours::background);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::highlightColourId, colours::accentFx.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, colours::text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.5f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float fromAngle = startAngle + zeroPos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::outline);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    g.setColour (slider.isEnabled() ? accent : colours::dim);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = arcR - lineW * 1.4f;
    g.setColour (colours::panel.brighter (0.12f));
    g.fillEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre));

    const auto tip = centre.getPointOnCircumference (knobR * 0.85f, angle);
    const auto mid = centre.getPointOnCircumference (knobR * 0.35f, angle);
    g.setColour (colours::text);
    g.drawLine ({ mid, tip }, 2.0f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();

    g.setColour (on ? accent.withAlpha (0.22f) : colours::background);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? accent : (highlighted ? colours::dim : colours::outline));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.2f);

    g.setColour (on ? colours::text : colours::dim);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accentFx : colours::outline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (colours::dim);
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return juce::FontOptions (13.0f); }
juce::Font LookAndFeel::getLabelFont (juce::Label& l)     { return l.getFont(); }

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
            const juce::String& caption, juce::Colour accent)
    : attachment (state, paramId, slider)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, colours::text);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    slider.setDoubleClickReturnValue (true, (double) state.getParameterRange (paramId)
                                                .convertFrom0to1 (state.getParameter (paramId)->getDefaultValue()));
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setTooltip (state.getParameter (paramId)->getName (64));

    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, colours::dim);
    label.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

//==============================================================================
Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                const juce::String& caption, const juce::StringArray& items)
    : attachment ((box.addItemList (items, 1), state), paramId, box)
{
    label.setText (caption, juce::dontSendNotification);
    label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, colours::dim);
    label.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (box);
    addAndMakeVisible (label);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    box.setBounds (r.removeFromTop (26));
}

//==============================================================================
Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                const juce::String& caption, juce::Colour accent)
    : attachment (state, paramId, button)
{
    button.setButtonText (caption);
    button.setColour (juce::ToggleButton::tickColourId, accent);
    addAndMakeVisible (button);
}

//==============================================================================
SpectrogramView::SpectrogramView (SpectralWeaverProcessor& p, int s, juce::String t, juce::Colour a)
    : processor (p), slot (s), title (std::move (t)), accent (a)
{
    lastPositions.fill (-1.0f);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refresh();
    startTimerHz (30);
}

void SpectrogramView::refresh()
{
    image = processor.getImage (slot);
    repaint();
}

juce::Rectangle<float> SpectrogramView::imageArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f).withTrimmedTop (22.0f).withTrimmedLeft (30.0f);
}

void SpectrogramView::timerCallback()
{
    bool changed = false;

    for (int v = 0; v < SpectralWeaverProcessor::numVoices; ++v)
    {
        const float pos = processor.getVoicePosition (v, slot);

        if (std::abs (pos - lastPositions[(size_t) v]) > 1.0e-4f)
        {
            lastPositions[(size_t) v] = pos;
            changed = true;
        }
    }

    if (changed || processor.isLoading (slot) || slot == SpectralWeaverProcessor::slotA)
        repaint();
}

void SpectrogramView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (dragOver ? accent : colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, dragOver ? 2.0f : 1.0f);

    // Header
    auto header = bounds.reduced (12.0f, 6.0f).removeFromTop (20.0f);
    g.setColour (accent);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title, header, juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (processor.getStatus (slot), header, juce::Justification::centredRight, true);

    const auto area = imageArea();

    // Frequency axis (matches renderSpectrogram: 30 Hz .. 20 kHz, log)
    g.setFont (juce::FontOptions (10.0f));

    for (const float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        const float norm = std::log (hz / 30.0f) / std::log (20000.0f / 30.0f);
        const float y = area.getBottom() - norm * area.getHeight();
        g.setColour (colours::dim);
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (area.getX() - 30.0f, y - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight);
    }

    if (image == nullptr || ! image->picture.isValid())
    {
        g.setColour (colours::background);
        g.fillRoundedRectangle (area, 6.0f);

        const float dash[] = { 6.0f, 5.0f };
        juce::Path outline;
        outline.addRoundedRectangle (area.reduced (1.0f), 6.0f);
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dash, 2);
        g.setColour (accent.withAlpha (0.45f));
        g.fillPath (dashed);

        g.setColour (colours::text.withAlpha (0.8f));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (processor.isLoading (slot) ? "Analysing spectrum..."
                                              : (slot == SpectralWeaverProcessor::slotA ? "Drop the source sample here"
                                                                                        : "Drop a modulator sample here"),
                    area.translated (0.0f, -10.0f), juce::Justification::centred);
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("or click to browse  -  WAV, AIFF, FLAC, OGG, MP3", area.translated (0.0f, 12.0f), juce::Justification::centred);
        return;
    }

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (area, 6.0f);
        g.reduceClipRegion (clip);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (image->picture, area);

        if (slot == SpectralWeaverProcessor::slotA)
        {
            // Dim everything outside the Start/Length scan region
            const float start  = processor.parameters.getRawParameterValue ("start")->load();
            const float length = processor.parameters.getRawParameterValue ("length")->load();
            const float x0 = area.getX() + start * area.getWidth();
            const float x1 = x0 + juce::jmax (0.002f, length * (1.0f - start)) * area.getWidth();

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRect (area.withRight (x0));
            g.fillRect (area.withLeft (juce::jmin (area.getRight(), x1)));

            g.setColour (accent.withAlpha (0.9f));
            g.fillRect (x0 - 1.0f, area.getY(), 2.0f, area.getHeight());

            if (processor.parameters.getRawParameterValue ("freeze")->load() > 0.5f)
            {
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText ("FROZEN", juce::Rectangle<float> (x0 + 4.0f, area.getY() + 4.0f, 60.0f, 14.0f), juce::Justification::centredLeft);
            }
        }

        if (processor.isLoading (slot))
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (area);
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText ("Analysing spectrum...", area, juce::Justification::centred);
        }

        // Play heads
        for (const float pos : lastPositions)
        {
            if (pos < 0.0f)
                continue;

            const float x = area.getX() + juce::jlimit (0.0f, 1.0f, pos) * area.getWidth();
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.fillRect (x - 3.0f, area.getY(), 6.0f, area.getHeight());
            g.setColour (juce::Colours::white);
            g.fillRect (x - 0.75f, area.getY(), 1.5f, area.getHeight());
        }
    }
}

void SpectrogramView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || (image != nullptr && e.mods.isCommandDown()))
    {
        juce::PopupMenu menu;
        menu.addItem ("Load sample...", [this] { browse(); });
        menu.addItem ("Clear", image != nullptr, false, [this] { processor.clearSample (slot); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }

    browse();
}

void SpectrogramView::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sample",
                                                   image != nullptr ? image->file.getParentDirectory() : juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SpectrogramView> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;

                              const auto file = fc.getResult();

                              if (file.existsAsFile())
                                  safe->processor.loadSample (safe->slot, file);
                          });
}

bool SpectrogramView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;

    return false;
}

void SpectrogramView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;

    for (const auto& f : files)
    {
        const juce::File file (f);

        if (file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
        {
            processor.loadSample (slot, file);
            break;
        }
    }

    repaint();
}

} // namespace ui

//==============================================================================
SpectralWeaverEditor::SpectralWeaverEditor (SpectralWeaverProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      viewA (p, SpectralWeaverProcessor::slotA, "A  /  SOURCE", ui::colours::accentA),
      viewB (p, SpectralWeaverProcessor::slotB, "B  /  MODULATOR", ui::colours::accentB),
      freeze   (p.parameters, "freeze",   "FREEZE",    ui::colours::accentA),
      loop     (p.parameters, "loop",     "LOOP",      ui::colours::accentA),
      keyTrack (p.parameters, "keyTrack", "KEY TRACK", ui::colours::accentA),
      syncB    (p.parameters, "syncB",    "SYNC TO A", ui::colours::accentB),
      crossMode (p.parameters, "crossMode", "MODE",   sw::crossModeNames()),
      lfoShape  (p.parameters, "lfoShape",  "SHAPE",  SpectralWeaverProcessor::lfoShapeNames()),
      lfoTarget (p.parameters, "lfoTarget", "TARGET", SpectralWeaverProcessor::lfoTargetNames()),
      keyboard (p.keyboardState, juce::KeyboardComponentBase::horizontalKeyboard)
{
    setLookAndFeel (&lnf);

    auto& s = p.parameters;
    using namespace ui::colours;

    playbackKnobs = { &knob (s, "start", "START", accentA),   &knob (s, "length", "LENGTH", accentA),
                      &knob (s, "speed", "SPEED", accentA),   &knob (s, "root", "ROOT", accentA),
                      &knob (s, "tune", "TUNE", accentA),     &knob (s, "bendRange", "BEND", accentA) };

    crossKnobs    = { &knob (s, "crossAmount", "AMOUNT", accentB), &knob (s, "envWidth", "ENV WIDTH", accentB),
                      &knob (s, "speedB", "B SPEED", accentB) };

    spectralKnobs = { &knob (s, "blur", "BLUR", accentFx),   &knob (s, "smear", "SMEAR", accentFx),
                      &knob (s, "tonal", "TONAL", accentFx), &knob (s, "tilt", "TILT", accentFx),
                      &knob (s, "gate", "GATE", accentFx),   &knob (s, "warp", "WARP", accentFx),
                      &knob (s, "shift", "SHIFT", accentFx), &knob (s, "phaseRand", "DIFFUSE", accentFx) };

    lfoKnobs      = { &knob (s, "lfoRate", "RATE", accentFx), &knob (s, "lfoDepth", "DEPTH", accentFx) };

    ampKnobs      = { &knob (s, "attack", "ATTACK", text), &knob (s, "decay", "DECAY", text),
                      &knob (s, "sustain", "SUSTAIN", text), &knob (s, "release", "RELEASE", text),
                      &knob (s, "gain", "GAIN", text) };

    for (auto* c : std::initializer_list<juce::Component*> { &viewA, &viewB, &freeze, &loop, &keyTrack, &syncB,
                                                             &crossMode, &lfoShape, &lfoTarget, &keyboard })
        addAndMakeVisible (c);

    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, accentA.withAlpha (0.6f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accentA.withAlpha (0.25f));

    processor.addChangeListener (this);
    setSize (1240, 728);
}

SpectralWeaverEditor::~SpectralWeaverEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void SpectralWeaverEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    viewA.refresh();
    viewB.refresh();
}

void SpectralWeaverEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour accent) const
{
    g.setColour (ui::colours::panel);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (ui::colours::outline);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (accent);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title, r.reduced (12, 6).removeFromTop (20), juce::Justification::centredLeft);
}

void SpectralWeaverEditor::paint (juce::Graphics& g)
{
    using namespace ui::colours;
    g.fillAll (background);

    auto header = getLocalBounds().removeFromTop (48).reduced (18, 0);
    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("SPECTRAL WEAVER", header, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("spectral resynthesis  /  cross-synthesis instrument", header.withTrimmedLeft (230), juce::Justification::centredLeft);
    g.drawText ("drop a sample on A  -  play MIDI  -  drop a second sample on B to modulate A",
                header, juce::Justification::centredRight);

    paintPanel (g, playbackPanel, "PLAYBACK  /  SCAN", accentA);
    paintPanel (g, crossPanel,    "B  ->  A  CROSS MODULATION", accentB);
    paintPanel (g, spectralPanel, "SPECTRAL SCULPTING", accentFx);
    paintPanel (g, lfoPanel,      "LFO", accentFx);
    paintPanel (g, ampPanel,      "AMP ENVELOPE", text);
}

void SpectralWeaverEditor::resized()
{
    const auto layoutRow = [] (juce::Rectangle<int> r, const std::vector<juce::Component*>& items)
    {
        if (items.empty())
            return;

        const int w = r.getWidth() / (int) items.size();

        for (auto* c : items)
            c->setBounds (r.removeFromLeft (w).reduced (3, 0));
    };

    auto r = getLocalBounds().reduced (16, 0);
    r.removeFromTop (48);

    auto keyboardArea = r.removeFromBottom (72).withTrimmedBottom (12);
    keyboard.setBounds (keyboardArea);
    keyboard.setKeyWidth (juce::jmax (12.0f, (float) keyboardArea.getWidth() / 43.0f));
    keyboard.setLowestVisibleKey (36);

    auto top = r.removeFromTop (250);
    r.removeFromTop (10);
    auto middle = r.removeFromTop (180);
    r.removeFromTop (10);
    auto bottom = r.removeFromTop (150);

    // Top: source view + playback
    playbackPanel = top.removeFromRight (440);
    top.removeFromRight (10);
    viewA.setBounds (top);

    {
        auto inner = playbackPanel.reduced (12).withTrimmedTop (22);
        auto row1  = inner.removeFromTop (92);
        layoutRow (row1, { playbackKnobs[0], playbackKnobs[1], playbackKnobs[2] });
        inner.removeFromTop (6);
        auto row2  = inner.removeFromTop (92);
        auto buttons = row2.removeFromRight (130).withSizeKeepingCentre (130, 92);
        layoutRow (row2, { playbackKnobs[3], playbackKnobs[4], playbackKnobs[5] });
        freeze  .setBounds (buttons.removeFromTop (26).reduced (4, 0));
        buttons.removeFromTop (6);
        loop    .setBounds (buttons.removeFromTop (26).reduced (4, 0));
        buttons.removeFromTop (6);
        keyTrack.setBounds (buttons.removeFromTop (26).reduced (4, 0));
    }

    // Middle: modulator view + cross modulation
    crossPanel = middle.removeFromRight (440);
    middle.removeFromRight (10);
    viewB.setBounds (middle);

    {
        auto inner = crossPanel.reduced (12).withTrimmedTop (24);
        auto left  = inner.removeFromLeft (130);
        crossMode.setBounds (left.removeFromTop (44));
        left.removeFromTop (14);
        syncB.setBounds (left.removeFromTop (26));
        inner.removeFromLeft (8);
        layoutRow (inner.withSizeKeepingCentre (inner.getWidth(), 92), crossKnobs);
    }

    // Bottom: spectral, LFO, amp
    spectralPanel = bottom.removeFromLeft (560);
    bottom.removeFromLeft (10);
    ampPanel = bottom.removeFromRight (330);
    bottom.removeFromRight (10);
    lfoPanel = bottom;

    layoutRow (spectralPanel.reduced (10).withTrimmedTop (24).withSizeKeepingCentre (spectralPanel.getWidth() - 20, 92), spectralKnobs);
    layoutRow (ampPanel.reduced (10).withTrimmedTop (24).withSizeKeepingCentre (ampPanel.getWidth() - 20, 92), ampKnobs);

    {
        auto inner = lfoPanel.reduced (12).withTrimmedTop (22);
        auto combos = inner.removeFromRight (inner.getWidth() / 2);
        lfoShape .setBounds (combos.removeFromTop (46).reduced (2, 0));
        combos.removeFromTop (8);
        lfoTarget.setBounds (combos.removeFromTop (46).reduced (2, 0));
        layoutRow (inner.withSizeKeepingCentre (inner.getWidth(), 92), lfoKnobs);
    }
}

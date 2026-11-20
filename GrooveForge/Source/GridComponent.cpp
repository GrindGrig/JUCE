#include "GridComponent.h"

namespace gf
{

static bool nearlyEqual (double a, double b) { return std::abs (a - b) < 1.0e-9; }

GridComponent::GridComponent (GrooveForgeProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    processor.addChangeListener (this);
    startTimerHz (30);
    updateSize();
}

GridComponent::~GridComponent()
{
    processor.removeChangeListener (this);
}

void GridComponent::setPixelsPerBeat (float ppb)
{
    pixelsPerBeat = juce::jlimit (24.0f, 600.0f, ppb);
    updateSize();
}

void GridComponent::updateSize()
{
    const auto w = (int) std::ceil (processor.getPattern().lengthInBeats() * pixelsPerBeat) + 1;
    setSize (w, laneHeight * NumLanes);
    repaint();
}

double GridComponent::snapBeat (double beat, bool free) const
{
    const auto& modes = snapModes();
    const double grid = modes[(size_t) juce::jlimit (0, (int) modes.size() - 1, processor.snapIndex)].beats;
    if (free || grid <= 0.0)
        return beat;
    return std::round (beat / grid) * grid;
}

juce::Rectangle<float> GridComponent::noteBounds (const DrumNote& n) const
{
    const float stepW = (float) (0.25 * pixelsPerBeat);
    const float w = juce::jmax (5.0f, stepW * 0.85f);
    return { beatToX (n.beat), (float) (n.lane * laneHeight) + 3.0f, w, (float) laneHeight - 6.0f };
}

int GridComponent::noteAt (juce::Point<float> p) const
{
    const auto& notes = processor.getPattern().notes;
    const int lane = yToLane (p.y);
    int best = -1;
    float bestDist = 1.0e9f;
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(size_t) i];
        if (n.lane != lane) continue;
        const auto r = noteBounds (n).expanded (2.0f, 0.0f);
        if (r.contains (p))
        {
            const float d = std::abs (r.getCentreX() - p.x);
            if (d < bestDist) { bestDist = d; best = i; }
        }
    }
    return best;
}

//==============================================================================
void GridComponent::paint (juce::Graphics& g)
{
    const auto& pat = processor.getPattern();
    const auto meter = Meter::create (pat.numerator, pat.denominator, pat.grouping);
    const double stepBeats = 0.25;
    const double barBeats = pat.beatsPerBar();
    const int h = getHeight();

    g.fillAll (Colours::background);

    // lane stripes
    for (int l = 0; l < NumLanes; ++l)
    {
        g.setColour (l % 2 == 0 ? juce::Colour (0xff1a1d23) : juce::Colour (0xff181a20));
        g.fillRect (0, l * laneHeight, getWidth(), laneHeight);
    }

    // beat-group shading (shows the grouping, e.g. 2+2+3) - alternate per tactus beat,
    // with a stronger tint at the start of each accent group
    for (int b = 0; b < pat.bars; ++b)
    {
        for (int t = 0; t < meter.numTactus(); ++t)
        {
            const double start = b * barBeats + meter.tactusStart[(size_t) t] * stepBeats;
            const double len = meter.tactusLen[(size_t) t] * stepBeats;
            const int group = meter.superGroupOfTactus (t);
            auto c = (group % 2 == 0) ? juce::Colour (0x0affffff) : juce::Colour (0x05ffffff);
            if (t % 2 == 1) c = c.withMultipliedAlpha (0.4f);
            g.setColour (c);
            g.fillRect (juce::Rectangle<float> (beatToX (start), 0.0f, beatToX (len), (float) h));
        }
    }

    // fine snap lines (only when the snap value is finer than a 16th or triplet-based)
    {
        const double grid = snapModes()[(size_t) juce::jlimit (0, (int) snapModes().size() - 1, processor.snapIndex)].beats;
        if (grid > 0.0 && ! nearlyEqual (std::fmod (stepBeats, grid), 0.0) && grid * pixelsPerBeat >= 5.0)
        {
            g.setColour (juce::Colour (0x10ffffff));
            for (double x = 0.0; x < pat.lengthInBeats(); x += grid)
                g.drawVerticalLine ((int) beatToX (x), 0.0f, (float) h);
        }
    }

    // step / tactus / bar lines
    for (int b = 0; b < pat.bars; ++b)
    {
        for (int s = 0; s < meter.stepsPerBar; ++s)
        {
            const auto& st = meter.steps[(size_t) s];
            const float x = beatToX (b * barBeats + s * stepBeats);
            if (st.barStart)          g.setColour (juce::Colour (0xff5a6070));
            else if (st.superStart)   g.setColour (juce::Colour (0xff3c414d));
            else if (st.tactusStart)  g.setColour (juce::Colour (0xff30343e));
            else                      g.setColour (juce::Colour (0xff22252c));
            g.drawVerticalLine ((int) x, 0.0f, (float) h);
            if (st.barStart)
                g.drawVerticalLine ((int) x + 1, 0.0f, (float) h);
        }
    }

    for (int l = 1; l < NumLanes; ++l)
    {
        g.setColour (juce::Colour (0xff101216));
        g.drawHorizontalLine (l * laneHeight, 0.0f, (float) getWidth());
    }

    // notes
    const auto& notes = pat.notes;
    const auto& lanes = processor.getLanes();
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(size_t) i];
        const auto r = noteBounds (n);
        const auto base = juce::Colour (laneInfo (n.lane).colour);
        const bool muted = lanes[(size_t) n.lane].muted;
        const float alpha = muted ? 0.25f : 0.35f + 0.65f * n.velocity;

        g.setColour (base.withAlpha (alpha * 0.55f));
        g.fillRoundedRectangle (r, 3.0f);

        // velocity bar
        auto vr = r.reduced (1.5f);
        vr = vr.withTop (vr.getBottom() - vr.getHeight() * n.velocity);
        g.setColour (base.withAlpha (alpha));
        g.fillRoundedRectangle (vr, 2.0f);

        // mark notes that sit clearly between 16ths (more than humanize drift)
        const bool offGrid = std::abs (n.beat * 4.0 - std::round (n.beat * 4.0)) > 0.08;
        if (offGrid)
        {
            // off-grid marker: a small diamond on top
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            juce::Path d;
            const float cx = r.getX() + 4.0f, cy = r.getY() + 4.0f;
            d.addTriangle (cx - 3.0f, cy, cx, cy - 3.0f, cx + 3.0f, cy);
            d.addTriangle (cx - 3.0f, cy, cx, cy + 3.0f, cx + 3.0f, cy);
            g.fillPath (d);
        }

        if (i == selectedIndex || i == hoverIndex)
        {
            g.setColour (i == selectedIndex ? juce::Colours::white : juce::Colours::white.withAlpha (0.4f));
            g.drawRoundedRectangle (r, 3.0f, i == selectedIndex ? 1.5f : 1.0f);
        }
    }

    // playhead
    const double ph = processor.playheadBeat.load();
    if (ph >= 0.0)
    {
        g.setColour (Colours::accent);
        g.fillRect (beatToX (ph) - 1.0f, 0.0f, 2.0f, (float) h);
    }
}

void GridComponent::timerCallback()
{
    const double ph = processor.playheadBeat.load();
    const float x = ph >= 0.0 ? beatToX (ph) : -1.0f;
    if (std::abs (x - lastPlayheadX) > 0.5f)
    {
        if (lastPlayheadX >= 0.0f) repaint ((int) lastPlayheadX - 3, 0, 7, getHeight());
        if (x >= 0.0f)             repaint ((int) x - 3, 0, 7, getHeight());
        lastPlayheadX = x;
    }
}

//==============================================================================
void GridComponent::showNoteStatus (int index)
{
    if (onStatus == nullptr) return;
    const auto& notes = processor.getPattern().notes;
    if (! juce::isPositiveAndBelow (index, (int) notes.size())) { onStatus ({}); return; }

    const auto& n = notes[(size_t) index];
    const auto& pat = processor.getPattern();
    const int bar = (int) (n.beat / pat.beatsPerBar());
    const double inBar = n.beat - bar * pat.beatsPerBar();
    const double sixteenth = inBar * 4.0;
    const int ticks = juce::roundToInt ((sixteenth - std::floor (sixteenth + 1.0e-6)) * 240.0);   // 960 PPQ -> 240 per 16th

    onStatus (juce::String (laneInfo (n.lane).name) + "  bar " + juce::String (bar + 1)
              + "  16th " + juce::String ((int) std::floor (sixteenth + 1.0e-6) + 1)
              + (ticks != 0 ? " +" + juce::String (ticks) + " ticks (off-grid)" : juce::String())
              + "  vel " + juce::String (juce::roundToInt (n.velocity * 127.0f)));
}

void GridComponent::mouseMove (const juce::MouseEvent& e)
{
    const int idx = noteAt (e.position);
    if (idx != hoverIndex) { hoverIndex = idx; repaint(); }
    setMouseCursor (idx >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void GridComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const int idx = noteAt (e.position);
    dragMoved = false;
    dragCreated = false;

    if (e.mods.isPopupMenu())
    {
        if (idx >= 0)
        {
            processor.modifyPattern ([idx] (Pattern& p) { p.notes.erase (p.notes.begin() + idx); });
            selectedIndex = hoverIndex = dragIndex = -1;
        }
        return;
    }

    if (idx >= 0)
    {
        dragIndex = selectedIndex = idx;
        const auto& n = processor.getPattern().notes[(size_t) idx];
        dragStartBeat = n.beat;
        dragStartVelocity = n.velocity;
        grabOffset = xToBeat (e.position.x) - n.beat;
        processor.previewLane (n.lane);
    }
    else
    {
        const int lane = yToLane (e.position.y);
        const double len = processor.getPattern().lengthInBeats();
        // click places the note at the cell under the cursor: snap the left edge
        double beat = e.mods.isAltDown() ? xToBeat (e.position.x)
                                         : std::floor (xToBeat (e.position.x) / juce::jmax (1.0e-6, snapModes()[(size_t) processor.snapIndex].beats)) * snapModes()[(size_t) processor.snapIndex].beats;
        if (snapModes()[(size_t) processor.snapIndex].beats <= 0.0)
            beat = xToBeat (e.position.x);
        beat = juce::jlimit (0.0, len - 1.0e-6, beat);

        const float vel = 0.8f;
        processor.modifyPattern ([=] (Pattern& p) { p.notes.push_back ({ lane, beat, 0.25, vel }); });

        // find it again after sorting
        const auto& notes = processor.getPattern().notes;
        for (int i = 0; i < (int) notes.size(); ++i)
            if (notes[(size_t) i].lane == lane && nearlyEqual (notes[(size_t) i].beat, beat))
                dragIndex = selectedIndex = i;

        dragStartBeat = beat;
        dragStartVelocity = vel;
        grabOffset = xToBeat (e.position.x) - beat;
        dragCreated = true;
        processor.previewLane (lane);
    }

    showNoteStatus (selectedIndex);
    repaint();
}

void GridComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0 || e.mods.isPopupMenu())
        return;

    if (e.getDistanceFromDragStart() > 3)
        dragMoved = true;
    if (! dragMoved)
        return;

    const auto& pat = processor.getPattern();
    if (! juce::isPositiveAndBelow (dragIndex, (int) pat.notes.size()))
        return;

    const auto current = pat.notes[(size_t) dragIndex];
    DrumNote updated = current;

    if (e.mods.isShiftDown())
    {
        updated.velocity = juce::jlimit (0.03f, 1.0f, dragStartVelocity - (float) e.getDistanceFromDragStartY() / 120.0f);
    }
    else
    {
        const double raw = xToBeat (e.position.x) - grabOffset;
        updated.beat = juce::jlimit (0.0, pat.lengthInBeats() - 1.0e-6, snapBeat (raw, e.mods.isAltDown()));
    }

    if (nearlyEqual (updated.beat, current.beat) && juce::approximatelyEqual (updated.velocity, current.velocity))
        return;

    const int idx = dragIndex;
    processor.modifyPattern ([idx, updated] (Pattern& p) { p.notes[(size_t) idx] = updated; });

    const auto& notes = processor.getPattern().notes;
    for (int i = 0; i < (int) notes.size(); ++i)
        if (notes[(size_t) i].lane == updated.lane && nearlyEqual (notes[(size_t) i].beat, updated.beat))
            dragIndex = selectedIndex = i;

    showNoteStatus (selectedIndex);
}

void GridComponent::mouseUp (const juce::MouseEvent& e)
{
    if (dragIndex >= 0 && ! dragMoved && ! dragCreated && ! e.mods.isPopupMenu())
    {
        // plain click on an existing note toggles it off, like a step sequencer
        const int idx = dragIndex;
        processor.modifyPattern ([idx] (Pattern& p)
        {
            if (juce::isPositiveAndBelow (idx, (int) p.notes.size()))
                p.notes.erase (p.notes.begin() + idx);
        });
        selectedIndex = hoverIndex = -1;
        showNoteStatus (-1);
    }
    dragIndex = -1;
    repaint();
}

void GridComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int idx = noteAt (e.position);
    if (idx < 0 || e.mods.isCommandDown())
    {
        if (e.mods.isCommandDown())
            setPixelsPerBeat (pixelsPerBeat * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f));
        else
            Component::mouseWheelMove (e, w);
        return;
    }

    processor.modifyPattern ([idx, w] (Pattern& p)
    {
        auto& n = p.notes[(size_t) idx];
        n.velocity = juce::jlimit (0.03f, 1.0f, n.velocity + (w.deltaY > 0 ? 0.05f : -0.05f));
    });
    selectedIndex = idx;
    showNoteStatus (idx);
}

bool GridComponent::keyPressed (const juce::KeyPress& k)
{
    if (! juce::isPositiveAndBelow (selectedIndex, (int) processor.getPattern().notes.size()))
        return false;

    const int idx = selectedIndex;
    auto n = processor.getPattern().notes[(size_t) idx];
    const double len = processor.getPattern().lengthInBeats();
    const double snap = snapModes()[(size_t) processor.snapIndex].beats;

    if (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey))
    {
        processor.modifyPattern ([idx] (Pattern& p) { p.notes.erase (p.notes.begin() + idx); });
        selectedIndex = -1;
        showNoteStatus (-1);
        return true;
    }

    if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::rightKey))
    {
        const double amount = k.getModifiers().isShiftDown() && snap > 0.0 ? snap : 1.0 / 128.0;
        n.beat = juce::jlimit (0.0, len - 1.0e-6, n.beat + (k.isKeyCode (juce::KeyPress::leftKey) ? -amount : amount));
    }
    else if (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::downKey))
    {
        n.velocity = juce::jlimit (0.03f, 1.0f, n.velocity + (k.isKeyCode (juce::KeyPress::upKey) ? 0.05f : -0.05f));
    }
    else
        return false;

    processor.modifyPattern ([idx, n] (Pattern& p) { p.notes[(size_t) idx] = n; });
    const auto& notes = processor.getPattern().notes;
    for (int i = 0; i < (int) notes.size(); ++i)
        if (notes[(size_t) i].lane == n.lane && nearlyEqual (notes[(size_t) i].beat, n.beat))
            selectedIndex = i;
    showNoteStatus (selectedIndex);
    return true;
}

//==============================================================================
LaneHeader::LaneHeader (GrooveForgeProcessor& p, int l) : processor (p), lane (l)
{
    nameButton.setButtonText (laneInfo (lane).name);
    nameButton.setTooltip ("Click to preview this sound");
    nameButton.onClick = [this] { processor.previewLane (lane); };

    muteButton.setClickingTogglesState (true);
    muteButton.setTooltip ("Mute layer (also excluded from MIDI export)");
    muteButton.onClick = [this] { processor.modifyLane (lane, [this] (LaneSettings& s) { s.muted = muteButton.getToggleState(); }); };

    lockButton.setClickingTogglesState (true);
    lockButton.setTooltip ("Lock layer: 'Generate' keeps it as it is");
    lockButton.onClick = [this] { processor.modifyLane (lane, [this] (LaneSettings& s) { s.locked = lockButton.getToggleState(); }); };

    diceButton.setTooltip ("Generate only this layer");
    diceButton.onClick = [this] { processor.generate (lane, true); };

    clearButton.setTooltip ("Clear this layer");
    clearButton.onClick = [this] { processor.clearLane (lane); };

    noteSlider.setSliderStyle (juce::Slider::IncDecButtons);
    noteSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    noteSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 34, 18);
    noteSlider.setRange (0, 127, 1);
    noteSlider.setTooltip ("MIDI note sent for this layer (default: General MIDI drum map)");
    noteSlider.onValueChange = [this] { processor.modifyLane (lane, [this] (LaneSettings& s) { s.midiNote = (int) noteSlider.getValue(); }); };

    for (auto* b : { &muteButton, &lockButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, b == &muteButton ? juce::Colour (0xffd04040) : juce::Colour (0xff3a8ad0));
    }

    for (juce::Component* c : { (juce::Component*) &nameButton, (juce::Component*) &muteButton, (juce::Component*) &lockButton,
                                (juce::Component*) &diceButton, (juce::Component*) &clearButton, (juce::Component*) &noteSlider })
        addAndMakeVisible (c);

    refresh();
}

void LaneHeader::refresh()
{
    const auto& s = processor.getLanes()[(size_t) lane];
    muteButton.setToggleState (s.muted, juce::dontSendNotification);
    lockButton.setToggleState (s.locked, juce::dontSendNotification);
    noteSlider.setValue (s.midiNote, juce::dontSendNotification);
}

void LaneHeader::paint (juce::Graphics& g)
{
    g.fillAll (lane % 2 == 0 ? Colours::panel : Colours::panel.darker (0.08f));
    g.setColour (juce::Colour (laneInfo (lane).colour));
    g.fillRect (0, 2, 4, getHeight() - 4);
}

void LaneHeader::resized()
{
    auto r = getLocalBounds().reduced (2).withTrimmedLeft (6);
    const int bw = 28;
    clearButton.setBounds (r.removeFromRight (bw));
    diceButton.setBounds (r.removeFromRight (bw + 12));
    lockButton.setBounds (r.removeFromRight (bw));
    muteButton.setBounds (r.removeFromRight (bw));
    r.removeFromRight (4);
    noteSlider.setBounds (r.removeFromRight (64));
    r.removeFromRight (4);
    nameButton.setBounds (r);
}

} // namespace gf

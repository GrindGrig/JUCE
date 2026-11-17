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

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    g.setColour (down ? accent.withAlpha (0.3f) : (highlighted ? colours::panel.brighter (0.15f) : colours::background));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (highlighted || down ? accent : colours::outline);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.1f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int) { return juce::FontOptions (11.5f, juce::Font::bold); }

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return juce::FontOptions (13.0f); }
juce::Font LookAndFeel::getLabelFont (juce::Label& l)     { return l.getFont(); }

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id,
            const juce::String& caption, juce::Colour accent)
    : paramId (id), attachment (state, id, slider)
{
    setComponentID (id);
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

void Knob::enableModulation (SpectralWeaverProcessor& p, int targetIndex)
{
    processor = &p;
    modIndex = targetIndex;
}

int Knob::lfoFromDescription (const juce::var& description)
{
    const auto text = description.toString();
    return text.startsWith ("lfo:") ? text.fromFirstOccurrenceOf (":", false, false).getIntValue() : -1;
}

bool Knob::needsLiveRepaint() const
{
    if (processor == nullptr)
        return false;

    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (const_cast<Knob*> (this)))
        if (c->isDragAndDropActive())
            return true;

    return processor->getModulatedNorm (modIndex) >= 0.0f;
}

bool Knob::isInterestedInDragSource (const SourceDetails& details)
{
    return processor != nullptr && lfoFromDescription (details.description) >= 0;
}

void Knob::itemDragEnter (const SourceDetails& details) { dragOverLfo = lfoFromDescription (details.description); repaint(); }
void Knob::itemDragExit (const SourceDetails&)          { dragOverLfo = -1; repaint(); }

void Knob::itemDropped (const SourceDetails& details)
{
    dragOverLfo = -1;

    if (const int lfo = lfoFromDescription (details.description); lfo >= 0 && processor != nullptr)
        processor->addRoute (lfo, paramId);

    repaint();
}

void Knob::paintOverChildren (juce::Graphics& g)
{
    if (processor == nullptr)
        return;

    // Drop-target highlight while an LFO is being dragged
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this); c != nullptr && c->isDragAndDropActive())
    {
        const int lfo = lfoFromDescription (c->getCurrentDragDescription());

        if (lfo >= 0)
        {
            const auto col = colours::lfo (lfo);
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (col.withAlpha (dragOverLfo >= 0 ? 0.25f : 0.08f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (col.withAlpha (dragOverLfo >= 0 ? 1.0f : 0.5f));
            g.drawRoundedRectangle (r, 8.0f, dragOverLfo >= 0 ? 2.0f : 1.0f);
        }
    }

    // Geometry matching LookAndFeel::drawRotarySlider
    const auto layout = slider.getLookAndFeel().getSliderLayout (slider);
    const auto bounds = layout.sliderBounds.toFloat().translated ((float) slider.getX(), (float) slider.getY()).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.5f, radius * 0.16f);
    const auto rotary = slider.getRotaryParameters();
    const auto angleAt = [&] (float n) { return rotary.startAngleRadians + n * (rotary.endAngleRadians - rotary.startAngleRadians); };
    const float base = (float) slider.valueToProportionOfLength (slider.getValue());

    for (int lfo = 0; lfo < sw::numLfos; ++lfo)
    {
        const float d = processor->parameters.getRawParameterValue (sw::modDepthId (lfo, paramId))->load();

        if (juce::exactlyEqual (d, 0.0f))
            continue;

        const bool uni = processor->parameters.getRawParameterValue (sw::lfoParamId (lfo, "Uni"))->load() > 0.5f;
        const float lo = juce::jlimit (0.0f, 1.0f, base + (uni ? juce::jmin (0.0f, d) : -std::abs (d)));
        const float hi = juce::jlimit (0.0f, 1.0f, base + (uni ? juce::jmax (0.0f, d) : std::abs (d)));
        const float r = radius + 1.5f + (float) lfo * 3.0f;

        juce::Path ring;
        ring.addCentredArc (centre.x, centre.y, r, r, 0.0f, angleAt (lo), angleAt (juce::jmax (hi, lo + 0.01f)), true);
        g.setColour (colours::lfo (lfo));
        g.strokePath (ring, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (const float live = processor->getModulatedNorm (modIndex); live >= 0.0f)
    {
        const auto dot = centre.getPointOnCircumference (radius - lineW * 0.5f, angleAt (live));
        g.setColour (colours::background);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (dot));
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    }
}

//==============================================================================
LfoDisplay::LfoDisplay (SpectralWeaverProcessor& p, int index) : processor (p), lfo (index)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

int LfoDisplay::currentShape() const
{
    return (int) processor.parameters.getRawParameterValue (sw::lfoParamId (lfo, "Shape"))->load();
}

juce::Point<float> LfoDisplay::toModel (const juce::MouseEvent& e) const
{
    const auto r = plot();
    float x = juce::jlimit (0.0f, 0.999f, (e.position.x - r.getX()) / r.getWidth());
    float y = juce::jlimit (-1.0f, 1.0f, 1.0f - 2.0f * (e.position.y - r.getY()) / r.getHeight());

    if (e.mods.isShiftDown())
    {
        x = juce::jmin (0.999f, std::round (x * 16.0f) / 16.0f);
        y = std::round (y * 4.0f) / 4.0f;
    }

    return { x, y };
}

juce::Point<float> LfoDisplay::toScreen (juce::Point<float> m) const
{
    const auto r = plot();
    return { r.getX() + m.x * r.getWidth(), r.getY() + (1.0f - m.y) * 0.5f * r.getHeight() };
}

int LfoDisplay::nearestPoint (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = 11.0f;

    for (size_t i = 0; i < editing.size(); ++i)
    {
        const float d = toScreen (editing[i]).getDistanceFrom (pos);

        if (d < bestDist)
        {
            bestDist = d;
            best = (int) i;
        }
    }

    return best;
}

void LfoDisplay::commit (juce::Point<float> tracked)
{
    std::sort (editing.begin(), editing.end(), [] (auto a, auto b) { return a.x < b.x; });
    dragIndex = -1;

    for (size_t i = 0; i < editing.size(); ++i)
        if (juce::exactlyEqual (editing[i].x, tracked.x) && juce::exactlyEqual (editing[i].y, tracked.y))
            dragIndex = (int) i;

    processor.setCustomPoints (lfo, editing);
    repaint();
}

void LfoDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (currentShape() != sw::custom)
    {
        // Drawing on a built-in shape turns it into an editable copy
        editing = sw::shapeToPoints (currentShape());

        if (auto* shapeParam = processor.parameters.getParameter (sw::lfoParamId (lfo, "Shape")))
        {
            shapeParam->beginChangeGesture();
            shapeParam->setValueNotifyingHost (shapeParam->convertTo0to1 ((float) sw::custom));
            shapeParam->endChangeGesture();
        }
    }
    else
    {
        editing = processor.getCustomPoints (lfo);
    }

    juce::Point<float> tracked;

    if (const int hit = nearestPoint (e.position); hit >= 0)
    {
        tracked = editing[(size_t) hit];
    }
    else
    {
        tracked = toModel (e);
        editing.push_back (tracked);
    }

    commit (tracked);
}

void LfoDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0 || dragIndex >= (int) editing.size())
        return;

    const auto moved = toModel (e);
    editing[(size_t) dragIndex] = moved;
    commit (moved);
}

void LfoDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (currentShape() != sw::custom)
        return;

    editing = processor.getCustomPoints (lfo);

    if (const int hit = nearestPoint (e.position); hit >= 0 && editing.size() > 2)
    {
        editing.erase (editing.begin() + hit);
        processor.setCustomPoints (lfo, editing);
        dragIndex = -1;
        repaint();
    }
}

void LfoDisplay::paint (juce::Graphics& g)
{
    const auto colour = colours::lfo (lfo);
    const auto r = plot();
    const int shape = currentShape();
    const auto points = processor.getCustomPoints (lfo);
    if (dragIndex < 0) editing = points;

    g.setColour (colours::background);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setColour (colours::outline);
    for (int q = 0; q <= 4; ++q)
        g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * (float) q / 4.0f), r.getY(), r.getBottom());
    for (const float y : { 1.0f, 0.0f, -1.0f })
        g.drawHorizontalLine (juce::roundToInt (toScreen ({ 0.0f, y }).y), r.getX(), r.getRight());

    juce::Path curve;

    for (int xi = 0; xi <= (int) r.getWidth(); ++xi)
    {
        const float x = (float) xi;
        const float v = sw::lfoShapeValue (shape, x / r.getWidth(), &points, true);
        const juce::Point<float> pt { r.getX() + x, toScreen ({ 0.0f, v }).y };
        xi == 0 ? curve.startNewSubPath (pt) : curve.lineTo (pt);
    }

    juce::Path fill (curve);
    fill.lineTo (r.getRight(), toScreen ({ 0.0f, 0.0f }).y);
    fill.lineTo (r.getX(), toScreen ({ 0.0f, 0.0f }).y);
    fill.closeSubPath();
    g.setColour (colour.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    if (shape == sw::custom)
    {
        for (auto pt : points)
        {
            const auto c = toScreen (pt);
            g.setColour (colours::background);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c));
            g.setColour (colour);
            g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c), 2.0f);
        }
    }

    // Live position
    const bool uni = processor.parameters.getRawParameterValue (sw::lfoParamId (lfo, "Uni"))->load() > 0.5f;
    const float out = processor.getLfoOutput (lfo);
    const float shown = juce::jlimit (-1.0f, 1.0f, uni ? out * 2.0f - 1.0f : out);
    const float x = r.getX() + processor.getLfoPhase (lfo) * r.getWidth();
    g.setColour (colours::text.withAlpha (0.8f));
    g.fillRect (x - 0.75f, r.getY(), 1.5f, r.getHeight());
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, toScreen ({ 0.0f, shown }).y }));

    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (uni ? "1" : "+1", r.withHeight (12.0f).translated (3.0f, 1.0f), juce::Justification::centredLeft);
    g.drawText (uni ? "0" : "-1", r.withTop (r.getBottom() - 12.0f).translated (3.0f, -1.0f), juce::Justification::centredLeft);

    const bool sync = processor.parameters.getRawParameterValue (sw::lfoParamId (lfo, "Sync"))->load() > 0.5f;
    const int div = (int) processor.parameters.getRawParameterValue (sw::lfoParamId (lfo, "Div"))->load();
    const float rateHz = processor.parameters.getRawParameterValue (sw::lfoParamId (lfo, "Rate"))->load();
    const auto rateText = sync ? juce::String (sw::lfoDivisions()[(size_t) juce::jlimit (0, (int) sw::lfoDivisions().size() - 1, div)].name)
                                     + " @ " + juce::String (processor.getHostBpm(), 0) + " BPM"
                               : juce::String (rateHz, 2) + " Hz";
    g.drawText (rateText, r.withTop (r.getBottom() - 12.0f).translated (-3.0f, -1.0f), juce::Justification::centredRight);
}

//==============================================================================
void LfoDragHandle::paint (juce::Graphics& g)
{
    const auto colour = colours::lfo (lfo);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (colour.withAlpha (isMouseOverOrDragging() ? 0.25f : 0.1f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

    juce::Path outline;
    outline.addRoundedRectangle (r, r.getHeight() * 0.5f);
    juce::Path dashed;
    const float dash[] { 4.0f, 3.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
    g.setColour (colour);
    g.fillPath (dashed);

    // grip dots
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (r.getX() + 10.0f + (float) j * 4.0f, r.getCentreY() - 5.0f + (float) i * 4.0f, 2.2f, 2.2f);

    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("DRAG ONTO A KNOB", r.withTrimmedLeft (22.0f), juce::Justification::centredLeft);
}

void LfoDragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 3)
        return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! container->isDragAndDropActive())
            container->startDragging ("lfo:" + juce::String (lfo), this);
}

//==============================================================================
RouteRow::RouteRow (SpectralWeaverProcessor& p, int lfo, const juce::String& targetId)
    : attachment (p.parameters, sw::modDepthId (lfo, targetId), depth),
      colour (colours::lfo (lfo))
{
    const int index = sw::modTargetIds().indexOf (targetId);
    name.setText (sw::modTargetNames()[index], juce::dontSendNotification);
    name.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    name.setColour (juce::Label::textColourId, colour);
    name.setMinimumHorizontalScale (0.7f);

    depth.setSliderStyle (juce::Slider::LinearHorizontal);
    depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 38, 18);
    depth.setColour (juce::Slider::trackColourId, colour);
    depth.setColour (juce::Slider::backgroundColourId, colours::outline);
    depth.setColour (juce::Slider::thumbColourId, colours::text);
    depth.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    depth.setColour (juce::Slider::textBoxTextColourId, colours::text);
    depth.setDoubleClickReturnValue (true, 0.5);
    depth.setTooltip ("Depth: how far LFO " + juce::String (lfo + 1) + " moves " + sw::modTargetNames()[index]
                      + ". Negative values invert it. Double-click for 50%.");

    remove.setTooltip ("Remove this target");
    remove.setColour (juce::TextButton::buttonOnColourId, colour);
    remove.setColour (juce::TextButton::textColourOffId, colours::dim);
    remove.onClick = [&p, lfo, targetId] { p.removeRoute (lfo, targetId); };

    addAndMakeVisible (name);
    addAndMakeVisible (depth);
    addAndMakeVisible (remove);
}

void RouteRow::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (colour.withAlpha (0.45f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
}

void RouteRow::resized()
{
    auto r = getLocalBounds().reduced (6, 2);
    remove.setBounds (r.removeFromRight (20).withSizeKeepingCentre (20, 18));
    name.setBounds (r.removeFromLeft (56));
    depth.setBounds (r);
}

//==============================================================================
LfoPanel::LfoPanel (SpectralWeaverProcessor& p, int index)
    : processor (p), lfo (index), colour (colours::lfo (index)), handle (index), display (p, index)
{
    auto& s = p.parameters;
    const auto id = [index] (const char* suffix) { return sw::lfoParamId (index, suffix); };

    shape    = std::make_unique<Choice> (s, id ("Shape"),  "SHAPE",    sw::lfoShapeNames());
    trigger  = std::make_unique<Choice> (s, id ("Retrig"), "TRIGGER",  sw::lfoTriggerNames());
    division = std::make_unique<Choice> (s, id ("Div"),    "DIVISION", sw::lfoDivisionNames());
    sync     = std::make_unique<Toggle> (s, id ("Sync"),   "SYNC",     colour);
    unipolar = std::make_unique<Toggle> (s, id ("Uni"),    "UNIPOLAR", colour);
    rate     = std::make_unique<Knob>   (s, id ("Rate"),   "RATE",     colour);
    phase    = std::make_unique<Knob>   (s, id ("Phase"),  "PHASE",    colour);
    smooth   = std::make_unique<Knob>   (s, id ("Smooth"), "SMOOTH",   colour);
    fade     = std::make_unique<Knob>   (s, id ("Fade"),   "FADE IN",  colour);

    sync->setTooltip ("Lock the rate to the host tempo (the LFO follows the song position while the host plays)");
    unipolar->setTooltip ("Unipolar moves knobs one way only (0 to +1); bipolar swings both ways (-1 to +1)");

    hint.setText ("Draw to customise. Double-click deletes a point. Shift snaps.", juce::dontSendNotification);
    hint.setFont (juce::FontOptions (11.0f));
    hint.setColour (juce::Label::textColourId, colours::dim);
    hint.setMinimumHorizontalScale (0.8f);

    reset.setTooltip ("Reset this LFO, remove its targets and restore the default custom shape");
    reset.setColour (juce::TextButton::buttonOnColourId, colour);
    reset.setColour (juce::TextButton::textColourOffId, colours::dim);
    reset.onClick = [this] { processor.resetGroup ("lfo" + juce::String (lfo + 1)); };

    emptyRoutes.setText ("No targets yet. Drag the handle onto a knob, or add one from the menu.", juce::dontSendNotification);
    emptyRoutes.setFont (juce::FontOptions (12.0f));
    emptyRoutes.setColour (juce::Label::textColourId, colours::dim);
    routesContent.addChildComponent (emptyRoutes);
    routesView.setViewedComponent (&routesContent, false);
    routesView.setScrollBarsShown (true, false);
    routesView.setScrollBarThickness (6);
    routesView.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, colours::dim.withAlpha (0.6f));

    addTarget.setTextWhenNothingSelected ("+ Add target");
    addTarget.onChange = [this]
    {
        if (const int sel = addTarget.getSelectedId(); sel > 0)
            processor.addRoute (lfo, sw::modTargetIds()[sel - 1]);
    };

    for (auto* c : std::initializer_list<juce::Component*> { &handle, &reset, &display, &hint, shape.get(), trigger.get(), division.get(),
                                                             sync.get(), unipolar.get(), rate.get(), phase.get(), smooth.get(), fade.get(),
                                                             &routesView, &addTarget })
        addAndMakeVisible (c);

    processor.parameters.state.addListener (this);
    handleAsyncUpdate();
}

LfoPanel::~LfoPanel()
{
    processor.parameters.state.removeListener (this);
}

void LfoPanel::handleAsyncUpdate()
{
    rows.clear();

    const auto routes = processor.getRoutes (lfo);

    for (const auto& target : routes)
    {
        auto row = std::make_unique<RouteRow> (processor, lfo, target);
        routesContent.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    emptyRoutes.setVisible (rows.empty());

    addTarget.clear (juce::dontSendNotification);

    for (int t = 0; t < sw::modTargetIds().size(); ++t)
        if (! routes.contains (sw::modTargetIds()[t]))
            addTarget.addItem (sw::modTargetNames()[t], t + 1);

    layoutRoutes();
}

void LfoPanel::layoutRoutes()
{
    const int width = juce::jmax (100, routesView.getWidth() - 8);
    constexpr int rowW = 180, rowH = 24, gap = 6;
    const int perLine = juce::jmax (1, (width + gap) / (rowW + gap));

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int col = (int) i % perLine, line = (int) i / perLine;
        rows[i]->setBounds (col * (rowW + gap), line * (rowH + gap), rowW, rowH);
    }

    const int lines = rows.empty() ? 1 : ((int) rows.size() + perLine - 1) / perLine;
    routesContent.setSize (width, lines * (rowH + gap) - gap);
    emptyRoutes.setBounds (0, 0, width, rowH);
}

void LfoPanel::refresh()
{
    const bool synced = processor.parameters.getRawParameterValue (sw::lfoParamId (lfo, "Sync"))->load() > 0.5f;
    rate->setEnabled (! synced);
    rate->setAlpha (synced ? 0.35f : 1.0f);
    division->setEnabled (synced);
    division->setAlpha (synced ? 1.0f : 0.4f);
    display.repaint();
}

void LfoPanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (colour);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("LFO " + juce::String (lfo + 1), getLocalBounds().reduced (12, 8).removeFromTop (22), juce::Justification::centredLeft);

    g.setColour (colours::outline);
    g.drawHorizontalLine (routesArea.getY() - 5, (float) routesArea.getX(), (float) routesArea.getRight());
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("TARGETS", routesArea.withWidth (60).withHeight (24), juce::Justification::centredLeft);
}

void LfoPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto header = r.removeFromTop (22);
    header.removeFromLeft (58);
    handle.setBounds (header.removeFromLeft (160));
    reset.setBounds (header.removeFromRight (62).withSizeKeepingCentre (62, 20));
    r.removeFromTop (8);

    routesArea = r.removeFromBottom (58);
    r.removeFromBottom (10);

    {
        auto area = routesArea;
        area.removeFromLeft (58);
        addTarget.setBounds (area.removeFromRight (124).removeFromTop (24));
        area.removeFromRight (8);
        routesView.setBounds (area);
        layoutRoutes();
    }

    auto left = r.removeFromLeft (juce::jmin (280, r.getWidth() / 2));
    hint.setBounds (left.removeFromBottom (16));
    left.removeFromBottom (2);
    display.setBounds (left);
    r.removeFromLeft (12);

    auto row1 = r.removeFromTop (44);
    const int third = row1.getWidth() / 3;
    shape   ->setBounds (row1.removeFromLeft (third).reduced (3, 0));
    trigger ->setBounds (row1.removeFromLeft (third).reduced (3, 0));
    division->setBounds (row1.reduced (3, 0));
    r.removeFromTop (4);

    auto toggles = r.removeFromRight (84).withSizeKeepingCentre (84, 54);
    sync    ->setBounds (toggles.removeFromTop (24));
    toggles.removeFromTop (6);
    unipolar->setBounds (toggles.removeFromTop (24));

    const int w = r.getWidth() / 4;
    for (auto* k : { rate.get(), phase.get(), smooth.get(), fade.get() })
        k->setBounds (r.removeFromLeft (w).reduced (1, 0));
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
    : processor (p), slot (s), title (std::move (t)), accent (a),
      normalise (p.parameters, SpectralWeaverProcessor::normaliseParamId (s), "NORMALIZE", a)
{
    lastPositions.fill (-1.0f);

    normalise.setTooltip ("Raise or lower this sample so its peak sits at 0 dB");
    swap.setTooltip ("Exchange the A and B samples");
    swap.setColour (juce::TextButton::buttonOnColourId, a);
    swap.setColour (juce::TextButton::textColourOffId, colours::dim);
    swap.onClick = [this] { processor.swapSamples(); };
    addAndMakeVisible (normalise);
    addAndMakeVisible (swap);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refresh();
    startTimerHz (30);
}

void SpectrogramView::refresh()
{
    image = processor.getImage (slot);
    normalise.setEnabled (image != nullptr);
    repaint();
}

void SpectrogramView::resized()
{
    auto header = getLocalBounds().reduced (12, 6).removeFromTop (20);
    header.removeFromLeft (140);
    normalise.setBounds (header.removeFromLeft (96));
    header.removeFromLeft (6);
    swap.setBounds (header.removeFromLeft (104));
    header.removeFromLeft (10);
    statusArea = header.toFloat();
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

    juce::ignoreUnused (changed);
    repaint(); // cheap, and keeps region, freeze and normalize state current
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

    auto statusText = processor.getStatus (slot);

    if (statusText.isEmpty() && image != nullptr)
    {
        const bool normalised = processor.parameters.getRawParameterValue (SpectralWeaverProcessor::normaliseParamId (slot))->load() > 0.5f;
        const float db = normalised ? 0.0f : juce::Decibels::gainToDecibels (image->samplePeak);
        statusText = image->name + juce::String::formatted ("  |  %.2fs  |  peak %+.1f dB", image->lengthSeconds, db);
    }

    g.drawText (statusText, statusArea, juce::Justification::centredRight, true);

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

    ampKnobs      = { &knob (s, "attack", "ATTACK", text), &knob (s, "decay", "DECAY", text),
                      &knob (s, "sustain", "SUSTAIN", text), &knob (s, "release", "RELEASE", text),
                      &knob (s, "gain", "GAIN", text) };

    for (auto* c : std::initializer_list<juce::Component*> { &viewA, &viewB, &freeze, &loop, &keyTrack, &syncB,
                                                             &crossMode, &keyboard })
        addAndMakeVisible (c);

    // Every knob that can be modulated accepts a dropped LFO
    for (auto& k : knobs)
        for (int t = 0; t < sw::modTargetIds().size(); ++t)
            if (k->getComponentID() == sw::modTargetIds()[t])
                k->enableModulation (p, t);

    for (int i = 0; i < sw::numLfos; ++i)
    {
        lfoPanels[(size_t) i] = std::make_unique<ui::LfoPanel> (p, i);
        addAndMakeVisible (*lfoPanels[(size_t) i]);
    }

    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, accentA.withAlpha (0.6f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accentA.withAlpha (0.25f));

    for (size_t i = 0; i < resetButtons.size(); ++i)
    {
        auto& b = resetButtons[i];
        b.setButtonText ("RESET");
        b.setTooltip ("Reset this section to its defaults");
        b.setColour (juce::TextButton::buttonOnColourId, accentFx);
        b.setColour (juce::TextButton::textColourOffId, dim);
        b.onClick = [this, id = juce::String (resetGroupIds[i])] { processor.resetGroup (id); };
        addAndMakeVisible (b);
    }

    resetAllButton.setTooltip ("Reset every knob, switch and menu to its default (samples stay loaded)");
    resetAllButton.setColour (juce::TextButton::buttonOnColourId, accentA);
    resetAllButton.setColour (juce::TextButton::textColourOffId, text);
    resetAllButton.onClick = [this] { processor.resetAll(); };
    addAndMakeVisible (resetAllButton);

    processor.addChangeListener (this);
    setSize (1240, 950);
    startTimerHz (30);
}

void SpectralWeaverEditor::timerCallback()
{
    for (auto& k : knobs)
        if (k->needsLiveRepaint())
            k->repaint();

    for (auto& panel : lfoPanels)
        panel->refresh();
}

SpectralWeaverEditor::~SpectralWeaverEditor()
{
    stopTimer();
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
                header.withTrimmedRight (124), juce::Justification::centredRight);

    paintPanel (g, playbackPanel, "PLAYBACK  /  SCAN", accentA);
    paintPanel (g, crossPanel,    "B  ->  A  CROSS MODULATION", accentB);
    paintPanel (g, spectralPanel, "SPECTRAL SCULPTING", accentFx);
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

    auto top = r.removeFromTop (230);
    r.removeFromTop (10);
    auto middle = r.removeFromTop (170);
    r.removeFromTop (10);
    auto bottom = r.removeFromTop (150);
    r.removeFromTop (10);
    auto mods = r.removeFromTop (250);

    lfoPanels[0]->setBounds (mods.removeFromLeft ((mods.getWidth() - 10) / 2));
    mods.removeFromLeft (10);
    lfoPanels[1]->setBounds (mods);

    // Top: source view + playback
    playbackPanel = top.removeFromRight (440);
    top.removeFromRight (10);
    viewA.setBounds (top);

    {
        auto inner = playbackPanel.reduced (12).withTrimmedTop (22);
        auto row1  = inner.removeFromTop (84);
        layoutRow (row1, { playbackKnobs[0], playbackKnobs[1], playbackKnobs[2] });
        inner.removeFromTop (6);
        auto row2  = inner.removeFromTop (84);
        auto buttons = row2.removeFromRight (130).withSizeKeepingCentre (130, 84);
        layoutRow (row2, { playbackKnobs[3], playbackKnobs[4], playbackKnobs[5] });
        freeze  .setBounds (buttons.removeFromTop (24).reduced (4, 0));
        buttons.removeFromTop (6);
        loop    .setBounds (buttons.removeFromTop (24).reduced (4, 0));
        buttons.removeFromTop (6);
        keyTrack.setBounds (buttons.removeFromTop (24).reduced (4, 0));
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

    // Bottom: spectral and amp
    spectralPanel = bottom.removeFromLeft ((bottom.getWidth() - 10) * 3 / 5);
    bottom.removeFromLeft (10);
    ampPanel = bottom;

    {
        const juce::Rectangle<int>* panels[] { &playbackPanel, &crossPanel, &spectralPanel, &ampPanel };

        for (size_t i = 0; i < resetButtons.size(); ++i)
            resetButtons[i].setBounds (panels[i]->getRight() - 12 - 62, panels[i]->getY() + 7, 62, 20);

        resetAllButton.setBounds (getWidth() - 18 - 110, 11, 110, 26);
    }

    layoutRow (spectralPanel.reduced (10).withTrimmedTop (24).withSizeKeepingCentre (spectralPanel.getWidth() - 20, 92), spectralKnobs);
    layoutRow (ampPanel.reduced (10).withTrimmedTop (24).withSizeKeepingCentre (ampPanel.getWidth() - 20, 92), ampKnobs);


}

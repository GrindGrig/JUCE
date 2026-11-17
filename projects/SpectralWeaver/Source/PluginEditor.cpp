#include "PluginEditor.h"

namespace ui
{

//==============================================================================
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
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    slider.setDoubleClickReturnValue (true, (double) state.getParameterRange (paramId)
                                                .convertFrom0to1 (state.getParameter (paramId)->getDefaultValue()));
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setTooltip (state.getParameter (paramId)->getName (64));

    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (9.5f, juce::Font::bold).withKerningFactor (0.1f));
    label.setColour (juce::Label::textColourId, colours::dim);
    label.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (13));
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
    const float lineW = cicada::LookAndFeel::arcThickness (radius);
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
        const float r = radius + 1.2f + (float) lfo * 2.6f;

        juce::Path ring;
        ring.addCentredArc (centre.x, centre.y, r, r, 0.0f, angleAt (lo), angleAt (juce::jmax (hi, lo + 0.01f)), true);
        g.setColour (colours::lfo (lfo));
        g.strokePath (ring, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

    g.setColour (colours::well);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

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
    g.setFont (juce::FontOptions (9.5f));
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
            g.fillEllipse (r.getX() + 8.0f + (float) j * 3.5f, r.getCentreY() - 4.5f + (float) i * 3.5f, 2.0f, 2.0f);

    g.setFont (juce::FontOptions (9.5f, juce::Font::bold).withKerningFactor (0.08f));
    g.drawText ("DRAG ONTO A KNOB", r.withTrimmedLeft (20.0f), juce::Justification::centredLeft);
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
    name.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    name.setColour (juce::Label::textColourId, colour);
    name.setMinimumHorizontalScale (0.7f);

    depth.setSliderStyle (juce::Slider::LinearHorizontal);
    depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 36, 16);
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
    g.setColour (colours::well);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (colour.withAlpha (0.4f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
}

void RouteRow::resized()
{
    auto r = getLocalBounds().reduced (6, 2);
    remove.setBounds (r.removeFromRight (18).withSizeKeepingCentre (18, 16));
    name.setBounds (r.removeFromLeft (48));
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
    hint.setFont (juce::FontOptions (10.0f));
    hint.setColour (juce::Label::textColourId, colours::dim);
    hint.setMinimumHorizontalScale (0.8f);

    reset.setTooltip ("Reset this LFO, remove its targets and restore the default custom shape");
    reset.setColour (juce::TextButton::buttonOnColourId, colour);
    reset.setColour (juce::TextButton::textColourOffId, colours::dim);
    reset.onClick = [this] { processor.resetGroup ("lfo" + juce::String (lfo + 1)); };

    emptyRoutes.setText ("No targets yet. Drag the handle onto a knob, or add one from the menu.", juce::dontSendNotification);
    emptyRoutes.setFont (juce::FontOptions (10.5f));
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
    const int width = juce::jmax (100, routesView.getWidth() - 6);
    constexpr int rowW = 162, rowH = 22, gap = 4;
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
    paintMembrane (g, getLocalBounds().toFloat());
    paintTitle (g, getLocalBounds().reduced (10, 6).removeFromTop (20).withWidth (60), "LFO " + juce::String (lfo + 1), colour);

    g.setColour (colours::outline);
    g.drawHorizontalLine (routesArea.getY() - 4, (float) routesArea.getX(), (float) routesArea.getRight());
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold).withKerningFactor (0.1f));
    g.drawText ("TARGETS", routesArea.withWidth (54).withHeight (22), juce::Justification::centredLeft);
}

void LfoPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto header = r.removeFromTop (20);
    header.removeFromLeft (58);
    handle.setBounds (header.removeFromLeft (140).withSizeKeepingCentre (140, 18));
    reset.setBounds (header.removeFromRight (54).withSizeKeepingCentre (54, 18));
    r.removeFromTop (4);

    routesArea = r.removeFromBottom (48);
    r.removeFromBottom (8);

    {
        auto area = routesArea;
        area.removeFromLeft (54);
        addTarget.setBounds (area.removeFromRight (112).removeFromTop (22));
        area.removeFromRight (6);
        routesView.setBounds (area);
        layoutRoutes();
    }

    auto left = r.removeFromLeft (juce::jmin (250, r.getWidth() / 2));
    hint.setBounds (left.removeFromBottom (13));
    left.removeFromBottom (2);
    display.setBounds (left);
    r.removeFromLeft (10);

    auto row1 = r.removeFromTop (36);
    const int third = row1.getWidth() / 3;
    shape   ->setBounds (row1.removeFromLeft (third).reduced (2, 0));
    trigger ->setBounds (row1.removeFromLeft (third).reduced (2, 0));
    division->setBounds (row1.reduced (2, 0));
    r.removeFromTop (2);

    auto toggles = r.removeFromRight (76).withSizeKeepingCentre (76, 48);
    sync    ->setBounds (toggles.removeFromTop (22));
    toggles.removeFromTop (4);
    unipolar->setBounds (toggles.removeFromTop (22));

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
    label.setFont (juce::FontOptions (9.5f, juce::Font::bold).withKerningFactor (0.1f));
    label.setColour (juce::Label::textColourId, colours::dim);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setBorderSize ({ 0, 1, 0, 0 });

    addAndMakeVisible (box);
    addAndMakeVisible (label);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (13));
    box.setBounds (r.removeFromTop (22));
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
    auto header = getLocalBounds().reduced (10, 5).removeFromTop (18);
    header.removeFromLeft (130);
    normalise.setBounds (header.removeFromLeft (84));
    header.removeFromLeft (4);
    swap.setBounds (header.removeFromLeft (92));
    header.removeFromLeft (8);
    statusArea = header.toFloat();
}

juce::Rectangle<float> SpectrogramView::imageArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f).withTrimmedTop (20.0f).withTrimmedLeft (26.0f);
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
    paintMembrane (g, bounds, dragOver);

    // Header
    paintTitle (g, getLocalBounds().reduced (10, 5).removeFromTop (18).withWidth (126), title, accent);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (10.5f));

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
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
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
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
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

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffdad7c2));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff12160e));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, background);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, panel);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, dim);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, accentA.withAlpha (0.75f));
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
    setSize (1100, 800);
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
    ui::paintMembrane (g, r.toFloat());
    ui::paintTitle (g, r.reduced (10, 5).removeFromTop (18).withTrimmedRight (60), title, accent);
}

void SpectralWeaverEditor::paint (juce::Graphics& g)
{
    using namespace ui::colours;

    cicada::paintVenation (g, getLocalBounds());
    cicada::paintHeader (g, getLocalBounds().removeFromTop (40).reduced (12, 0), "Spectral Weaver", "spectral resynthesis instrument");

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

    constexpr int gap = 8, knobH = 74;

    auto r = getLocalBounds().reduced (12, 0);
    r.removeFromTop (46);

    auto keyboardArea = r.removeFromBottom (62).withTrimmedBottom (8);
    keyboard.setBounds (keyboardArea);
    keyboard.setKeyWidth (juce::jmax (11.0f, (float) keyboardArea.getWidth() / 43.0f));
    keyboard.setLowestVisibleKey (36);

    auto top = r.removeFromTop (190);
    r.removeFromTop (gap);
    auto middle = r.removeFromTop (136);
    r.removeFromTop (gap);
    auto bottom = r.removeFromTop (114);
    r.removeFromTop (gap);
    auto mods = r.removeFromTop (204);

    lfoPanels[0]->setBounds (mods.removeFromLeft ((mods.getWidth() - gap) / 2));
    mods.removeFromLeft (gap);
    lfoPanels[1]->setBounds (mods);

    // Top: source view + playback
    playbackPanel = top.removeFromRight (390);
    top.removeFromRight (gap);
    viewA.setBounds (top);

    {
        auto inner = playbackPanel.reduced (10, 6).withTrimmedTop (20);
        auto row1  = inner.removeFromTop (knobH);
        layoutRow (row1, { playbackKnobs[0], playbackKnobs[1], playbackKnobs[2] });
        inner.removeFromTop (4);
        auto row2  = inner.removeFromTop (knobH);
        auto buttons = row2.removeFromRight (112).withSizeKeepingCentre (112, 74);
        layoutRow (row2, { playbackKnobs[3], playbackKnobs[4], playbackKnobs[5] });
        freeze  .setBounds (buttons.removeFromTop (22).reduced (3, 0));
        buttons.removeFromTop (4);
        loop    .setBounds (buttons.removeFromTop (22).reduced (3, 0));
        buttons.removeFromTop (4);
        keyTrack.setBounds (buttons.removeFromTop (22).reduced (3, 0));
    }

    // Middle: modulator view + cross modulation
    crossPanel = middle.removeFromRight (390);
    middle.removeFromRight (gap);
    viewB.setBounds (middle);

    {
        auto inner = crossPanel.reduced (10, 6).withTrimmedTop (22);
        auto left  = inner.removeFromLeft (118);
        crossMode.setBounds (left.removeFromTop (36));
        left.removeFromTop (10);
        syncB.setBounds (left.removeFromTop (22));
        inner.removeFromLeft (6);
        layoutRow (inner.withSizeKeepingCentre (inner.getWidth(), knobH), crossKnobs);
    }

    // Bottom: spectral and amp
    spectralPanel = bottom.removeFromLeft ((bottom.getWidth() - gap) * 3 / 5);
    bottom.removeFromLeft (gap);
    ampPanel = bottom;

    {
        const juce::Rectangle<int>* panels[] { &playbackPanel, &crossPanel, &spectralPanel, &ampPanel };

        for (size_t i = 0; i < resetButtons.size(); ++i)
            resetButtons[i].setBounds (panels[i]->getRight() - 10 - 54, panels[i]->getY() + 5, 54, 18);

        resetAllButton.setBounds (getWidth() - 12 - 92, 9, 92, 22);
    }

    layoutRow (spectralPanel.reduced (8, 6).withTrimmedTop (20).withSizeKeepingCentre (spectralPanel.getWidth() - 16, knobH), spectralKnobs);
    layoutRow (ampPanel.reduced (8, 6).withTrimmedTop (20).withSizeKeepingCentre (ampPanel.getWidth() - 16, knobH), ampKnobs);


}

/*
  ==============================================================================

   Detail Clipper editor, in the Cicada look.

  ==============================================================================
*/

#include "PluginEditor.h"

namespace detail_clipper
{

namespace
{
    constexpr int editorWidth  = 880;
    constexpr int editorHeight = 520;

    juce::Font captionFont()  { return juce::FontOptions (9.5f, juce::Font::bold).withKerningFactor (0.1f); }
    juce::Font monoFont()     { return juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain); }

    void fillWell (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (cicada::colours::well);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
    }

    void styleCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text.toUpperCase(), juce::dontSendNotification);
        l.setFont (captionFont());
        l.setColour (juce::Label::textColourId, cicada::colours::sage);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setBorderSize ({});
        l.setInterceptsMouseClicks (false, false);
    }
}

//==============================================================================
KnobCell::KnobCell (juce::AudioProcessorValueTreeState& state, const char* paramID, const juce::String& text, juce::Colour accent)
{
    styleCaption (caption, text);
    caption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (caption);

    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 14);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, cicada::colours::ink);
    slider.setMouseDragSensitivity (180);
    slider.setVelocityBasedMode (false);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);

    if (auto* param = state.getParameter (paramID))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void KnobCell::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (12));
    slider.setBounds (r);
}

//==============================================================================
DetailClipperEditor::DetailClipperEditor (DetailClipperAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      drive          (p.apvts, ParamIDs::drive,          "Drive",    cicada::colours::gold),
      ceiling        (p.apvts, ParamIDs::ceiling,        "Ceiling",  cicada::colours::gold),
      knee           (p.apvts, ParamIDs::knee,           "Knee",     cicada::colours::gold),
      asymmetry      (p.apvts, ParamIDs::asymmetry,      "Asym",     cicada::colours::gold),
      detail         (p.apvts, ParamIDs::detail,         "Detail",   cicada::colours::gold),
      detailFreq     (p.apvts, ParamIDs::detailFreq,     "Freq",     cicada::colours::gold),
      detailHeadroom (p.apvts, ParamIDs::detailHeadroom, "Headroom", cicada::colours::gold),
      output         (p.apvts, ParamIDs::outputGain,     "Output",   cicada::colours::ink),
      mix            (p.apvts, ParamIDs::mix,            "Mix",      cicada::colours::ink)
{
    setLookAndFeel (&lnf);

    const std::array<std::pair<const char*, juce::Colour>, numSections> sectionInfo {{
        { "Clip",   cicada::colours::gold },
        { "Shape",  cicada::colours::gold },
        { "Detail", cicada::colours::gold },
        { "Output", cicada::colours::ink }
    }};

    for (int i = 0; i < numSections; ++i)
    {
        auto& panel = panels[(size_t) i];
        panel.title  = sectionInfo[(size_t) i].first;
        panel.accent = sectionInfo[(size_t) i].second;
        panel.reset.setColour (juce::TextButton::buttonOnColourId, panel.accent);
        panel.reset.setColour (juce::TextButton::textColourOffId, cicada::colours::sage);
        panel.reset.setTooltip ("Reset this section");
        panel.reset.onClick = [this, i] { resetSection (i); };
        addAndMakeVisible (panel.reset);
    }

    resetAll.setColour (juce::TextButton::buttonOnColourId, cicada::colours::eye);
    resetAll.setColour (juce::TextButton::textColourOffId, cicada::colours::ink);
    resetAll.onClick = [this] { for (int i = 0; i < numSections; ++i) resetSection (i); };
    addAndMakeVisible (resetAll);

    for (auto* k : { &drive, &ceiling, &knee, &asymmetry, &detail, &detailFreq, &detailHeadroom, &output, &mix })
        addAndMakeVisible (*k);

    styleCaption (oversamplingCaption, "Oversampling");
    styleCaption (stereoCaption, "Stereo");
    addAndMakeVisible (oversamplingCaption);
    addAndMakeVisible (stereoCaption);
    addCombo (oversamplingBox, oversamplingAttachment, ParamIDs::oversampling)
        .setTooltip ("Higher factors alias less and cost more CPU");
    addCombo (stereoBox, stereoAttachment, ParamIDs::stereoMode)
        .setTooltip ("Mid/Side clips the mid and side separately and keeps the stereo image steadier");

    addPill (adaaPill,        adaaAttachment,        ParamIDs::adaa,        cicada::colours::gold, "Antiderivative anti-aliasing");
    addPill (trueCeilingPill, trueCeilingAttachment, ParamIDs::trueCeiling, cicada::colours::gold,
             "Catches the small overshoot of the oversampling filters. Brings back a little aliasing.");
    addPill (autoGainPill,    autoGainAttachment,    ParamIDs::autoGain,    cicada::colours::ink,  "Removes the drive gain for a fair A/B");
    addPill (deltaPill,       deltaAttachment,       ParamIDs::delta,       cicada::colours::ink,  "Hear only what the clipper removes");

    setSize (editorWidth, editorHeight);
    startTimerHz (30);
}

DetailClipperEditor::~DetailClipperEditor()
{
    setLookAndFeel (nullptr);
}

juce::ToggleButton& DetailClipperEditor::addPill (juce::ToggleButton& b,
                                                  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& attachment,
                                                  const char* paramID, juce::Colour accent, const juce::String& tooltip)
{
    b.setColour (juce::ToggleButton::tickColourId, accent);
    b.setTooltip (tooltip);
    addAndMakeVisible (b);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, paramID, b);
    return b;
}

juce::ComboBox& DetailClipperEditor::addCombo (juce::ComboBox& box,
                                               std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& attachment,
                                               const char* paramID)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramID)))
        box.addItemList (param->choices, 1);

    addAndMakeVisible (box);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, paramID, box);
    return box;
}

//==============================================================================
void DetailClipperEditor::resetSection (int section)
{
    static const std::array<std::vector<const char*>, numSections> ids {{
        { ParamIDs::drive, ParamIDs::ceiling, ParamIDs::oversampling, ParamIDs::adaa },
        { ParamIDs::knee, ParamIDs::asymmetry, ParamIDs::stereoMode },
        { ParamIDs::detail, ParamIDs::detailFreq, ParamIDs::detailHeadroom, ParamIDs::trueCeiling },
        { ParamIDs::outputGain, ParamIDs::mix, ParamIDs::autoGain, ParamIDs::delta }
    }};

    for (auto* id : ids[(size_t) section])
    {
        if (auto* param = processor.apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getDefaultValue());
            param->endChangeGesture();
        }
    }

    flash (section);
}

void DetailClipperEditor::flash (int section)
{
    panels[(size_t) section].flashStart = juce::Time::getMillisecondCounterHiRes();
}

//==============================================================================
void DetailClipperEditor::timerCallback()
{
    // pull new scope samples
    auto& fifo = processor.scopeFifo;
    const auto ready = fifo.getNumReady();

    if (ready > 0)
    {
        const auto keep = juce::jmin (ready, scopeLength);
        std::move (scopeDriven.begin() + keep, scopeDriven.end(), scopeDriven.begin());
        std::move (scopeOutput.begin() + keep, scopeOutput.end(), scopeOutput.begin());

        const auto scope = fifo.read (ready);
        int written = 0;

        auto take = [&] (int start, int size)
        {
            for (int i = 0; i < size; ++i, ++written)
            {
                const auto dst = written - (ready - keep);   // only the newest `keep` samples are stored

                if (dst >= 0)
                {
                    scopeDriven[(size_t) (scopeLength - keep + dst)] = processor.scopeDriven[(size_t) (start + i)];
                    scopeOutput[(size_t) (scopeLength - keep + dst)] = processor.scopeOutput[(size_t) (start + i)];
                }
            }
        };

        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }

    const auto clipDb = juce::Decibels::gainToDecibels (1.0f + processor.clipAmountMeter.load(), 0.0f);
    displayedClipDb = juce::jmax (clipDb, displayedClipDb * 0.96f);
    displayedIn  = processor.inputPeakMeter.load();
    displayedOut = processor.outputPeakMeter.load();

    repaint();
}

//==============================================================================
void DetailClipperEditor::paint (juce::Graphics& g)
{
    cicada::paintVenation (g, getLocalBounds());
    cicada::paintHeader (g, getLocalBounds().reduced (12, 0).removeFromTop (40),
                         "Detail Clipper", "hard clipping that keeps the transients");

    const auto now = juce::Time::getMillisecondCounterHiRes();

    for (auto& panel : panels)
    {
        cicada::paintMembrane (g, panel.bounds.toFloat());
        cicada::paintTitle (g, panel.bounds.reduced (10, 7).removeFromTop (18), panel.title.toUpperCase(), panel.accent);

        if (panel.flashStart >= 0.0)
        {
            const auto t = (now - panel.flashStart) / 600.0;

            if (t < 1.0)
            {
                g.setColour (panel.accent.withAlpha ((float) (1.0 - t)));
                g.drawRoundedRectangle (panel.bounds.toFloat().reduced (0.5f), 9.0f, 1.6f);
            }
            else
            {
                panel.flashStart = -1.0;
            }
        }
    }

    drawTransferCurve (g, curveWell.toFloat());
    drawScope (g, scopeWell.toFloat());
    drawClipMeter (g, clipMeterArea);

    auto dbText = [] (float gain)
    {
        return gain > 1.0e-5f ? juce::String (juce::Decibels::gainToDecibels (gain), 1) : juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e"));
    };

    g.setFont (monoFont());
    g.setColour (cicada::colours::sage);
    g.drawText ("In " + dbText (displayedIn) + "  Out " + dbText (displayedOut) + " dBFS", ioArea, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText ("Clipped content above Freq comes back, up to Headroom over the ceiling.",
                      detailHelpArea, juce::Justification::topLeft, 3, 1.0f);
    g.drawFittedText ("How far the driven peaks go over the ceiling. Double-click a knob to reset it.",
                      clipHelpArea, juce::Justification::topLeft, 3, 1.0f);
}

void DetailClipperEditor::drawTransferCurve (juce::Graphics& g, juce::Rectangle<float> r) const
{
    fillWell (g, r);

    const auto p = processor.getDisplayParams();
    constexpr float range = 1.25f;
    const auto inner = r.reduced (8.0f);

    auto toScreen = [&] (float x, float y)
    {
        return juce::Point<float> (juce::jmap (x, -range, range, inner.getX(), inner.getRight()),
                                   juce::jmap (y, -range, range, inner.getBottom(), inner.getY()));
    };

    g.setColour (cicada::colours::line);
    g.drawLine ({ toScreen (-range, 0.0f), toScreen (range, 0.0f) });
    g.drawLine ({ toScreen (0.0f, -range), toScreen (0.0f, range) });
    g.drawLine ({ toScreen (-range, -range), toScreen (range, range) });

    const auto t  = p.curve.threshold;
    const auto hr = juce::jmin (p.headroomLimit, range);
    const float dashes[] { 3.0f, 4.0f };

    g.setColour (cicada::colours::vein.withAlpha (0.45f));
    g.drawDashedLine ({ toScreen (-range, hr),  toScreen (range, hr) },  dashes, 2);
    g.drawDashedLine ({ toScreen (-range, -hr), toScreen (range, -hr) }, dashes, 2);

    g.setColour (cicada::colours::gold.withAlpha (0.35f));
    g.drawLine ({ toScreen (-range, t),  toScreen (range, t) });
    g.drawLine ({ toScreen (-range, -t), toScreen (range, -t) });

    juce::Path path;
    constexpr int steps = 300;

    for (int i = 0; i <= steps; ++i)
    {
        const auto x  = juce::jmap ((float) i, 0.0f, (float) steps, -range, range);
        const auto y  = p.curve.process (x * p.drive + p.bias) - p.biasOffset;
        const auto pt = toScreen (x, juce::jlimit (-range, range, y));

        if (i == 0) path.startNewSubPath (pt);
        else        path.lineTo (pt);
    }

    g.setColour (cicada::colours::gold.withAlpha (0.25f));
    g.strokePath (path, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (cicada::colours::gold);
    g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont (monoFont());
    g.setColour (cicada::colours::sage);
    const auto ceilingDb = juce::Decibels::gainToDecibels (t);
    g.drawText ("ceiling " + juce::String (ceilingDb, 1) + " dB",
                juce::Rectangle<float> (inner.getX(), toScreen (0.0f, t).y - 15.0f, inner.getWidth(), 13.0f),
                juce::Justification::centredLeft);
}

void DetailClipperEditor::drawScope (juce::Graphics& g, juce::Rectangle<float> r) const
{
    fillWell (g, r);

    constexpr float range = 1.6f;
    const auto inner = r.reduced (2.0f, 6.0f);
    auto yFor = [&] (float v) { return inner.getCentreY() - juce::jlimit (-range, range, v) / range * inner.getHeight() * 0.5f; };

    g.setColour (cicada::colours::line);
    g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());

    const auto t = juce::Decibels::decibelsToGain (processor.apvts.getRawParameterValue (ParamIDs::ceiling)->load());
    const float dashes[] { 3.0f, 4.0f };
    g.setColour (cicada::colours::gold.withAlpha (0.35f));
    g.drawDashedLine ({ inner.getX(), yFor (t),  inner.getRight(), yFor (t) },  dashes, 2);
    g.drawDashedLine ({ inner.getX(), yFor (-t), inner.getRight(), yFor (-t) }, dashes, 2);

    auto trace = [&] (const std::vector<float>& data, juce::Colour colour, float width)
    {
        juce::Path path;
        const auto n = (int) data.size();

        for (int i = 0; i < n; ++i)
        {
            const auto x = inner.getX() + inner.getWidth() * (float) i / (float) (n - 1);
            if (i == 0) path.startNewSubPath (x, yFor (data[(size_t) i]));
            else        path.lineTo (x, yFor (data[(size_t) i]));
        }

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (width));
    };

    trace (scopeDriven, cicada::colours::eye.withAlpha (0.7f), 1.1f);
    trace (scopeOutput, cicada::colours::ink, 1.4f);

    g.setFont (monoFont());
    const auto legend = r.reduced (8.0f, 4.0f);
    g.setColour (cicada::colours::eye);
    g.drawText ("driven input", legend.withTrimmedBottom (14.0f), juce::Justification::bottomRight);
    g.setColour (cicada::colours::ink);
    g.drawText ("output", legend, juce::Justification::bottomRight);
}

void DetailClipperEditor::drawClipMeter (juce::Graphics& g, juce::Rectangle<int> area) const
{
    auto r = area;
    g.setFont (captionFont());
    g.setColour (cicada::colours::sage);
    g.drawText ("CLIP", r.removeFromLeft (34), juce::Justification::centredLeft);

    g.setFont (monoFont());
    g.setColour (cicada::colours::ink);
    g.drawText (juce::String (displayedClipDb, 1) + " dB", r.removeFromRight (56), juce::Justification::centredRight);

    const auto bar = r.reduced (4, 0).withSizeKeepingCentre (r.getWidth() - 8, 6).toFloat();
    g.setColour (cicada::colours::well);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (cicada::colours::line);
    g.drawRoundedRectangle (bar, 3.0f, 1.0f);

    const auto fill = bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, displayedClipDb / 24.0f));
    g.setColour (cicada::colours::gold.withAlpha (0.3f));
    g.fillRoundedRectangle (fill.expanded (0.0f, 2.0f), 4.0f);
    g.setColour (cicada::colours::gold);
    g.fillRoundedRectangle (fill, 3.0f);
}

//==============================================================================
void DetailClipperEditor::resized()
{
    auto area = getLocalBounds().reduced (12, 0);
    auto header = area.removeFromTop (40);
    resetAll.setBounds (header.removeFromRight (72).withSizeKeepingCentre (72, 22));

    area.removeFromTop (8);
    area.removeFromBottom (12);

    auto top = area.removeFromTop (268);
    area.removeFromTop (8);

    panels[clipSection].bounds  = top.removeFromLeft (232);
    top.removeFromLeft (8);
    panels[shapeSection].bounds = top.removeFromLeft (340);
    top.removeFromLeft (8);
    panels[detailSection].bounds = top;
    panels[outputSection].bounds = area;

    // panel interiors; returns the content area below the title row
    auto interior = [this] (int section)
    {
        auto inner = panels[(size_t) section].bounds.reduced (10, 7);
        auto titleRow = inner.removeFromTop (18);
        panels[(size_t) section].reset.setBounds (titleRow.removeFromRight (48));
        inner.removeFromTop (6);
        return inner;
    };

    constexpr int knobW = 64, knobH = 82, gap = 6;

    auto placeKnobs = [&] (juce::Rectangle<int> row, std::initializer_list<KnobCell*> knobs)
    {
        for (auto* k : knobs)
        {
            k->setBounds (row.removeFromLeft (knobW).withHeight (knobH));
            row.removeFromLeft (gap);
        }
    };

    {   // Clip
        auto r = interior (clipSection);
        placeKnobs (r.removeFromTop (knobH), { &drive, &ceiling });
        r.removeFromTop (gap);
        oversamplingCaption.setBounds (r.removeFromTop (12));
        r.removeFromTop (2);
        auto row = r.removeFromTop (22);
        oversamplingBox.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (gap);
        adaaPill.setBounds (row.removeFromLeft (64));
        r.removeFromTop (14);
        clipMeterArea = r.removeFromTop (14);
        r.removeFromTop (10);
        clipHelpArea = r.removeFromTop (44);
    }

    {   // Shape
        auto r = interior (shapeSection);
        curveWell = r.removeFromTop (138);
        r.removeFromTop (gap);
        placeKnobs (r.removeFromLeft (2 * (knobW + gap)), { &knee, &asymmetry });
        auto stereoCol = r.removeFromRight (110).withSizeKeepingCentre (110, 36);
        stereoCaption.setBounds (stereoCol.removeFromTop (12));
        stereoCol.removeFromTop (2);
        stereoBox.setBounds (stereoCol);
    }

    {   // Detail
        auto r = interior (detailSection);
        placeKnobs (r.removeFromTop (knobH), { &detail, &detailFreq, &detailHeadroom });
        r.removeFromTop (gap + 4);
        trueCeilingPill.setBounds (r.removeFromTop (22).removeFromLeft (112));
        r.removeFromTop (10);
        detailHelpArea = r.removeFromTop (54);
    }

    {   // Output
        const auto titleRow = panels[outputSection].bounds.reduced (10, 7).removeFromTop (18);
        ioArea = titleRow.withTrimmedLeft (90).withWidth (260);

        auto r = interior (outputSection);
        auto right = r.removeFromRight (2 * (knobW + gap) + 40);
        r.removeFromRight (10);
        scopeWell = r;

        placeKnobs (right.removeFromTop (knobH).withTrimmedLeft (20), { &output, &mix });
        right.removeFromTop (gap + 4);
        auto pills = right.removeFromTop (22);
        autoGainPill.setBounds (pills.removeFromLeft (88));
        pills.removeFromLeft (gap);
        deltaPill.setBounds (pills.removeFromLeft (64));
    }
}

} // namespace detail_clipper

//==============================================================================
juce::AudioProcessorEditor* detail_clipper::DetailClipperAudioProcessor::createEditor()
{
    return new DetailClipperEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new detail_clipper::DetailClipperAudioProcessor();
}

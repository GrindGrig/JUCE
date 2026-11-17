#ifdef CICADA_UI_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of JUCE cpp file"
#endif

#include "cicada_ui.h"

namespace cicada
{

void paintMembrane (juce::Graphics& g, juce::Rectangle<float> r, bool highlighted)
{
    juce::ColourGradient membrane (colours::membraneHi.withAlpha (0.92f), r.getX(), r.getY(),
                                   colours::membrane.darker (0.35f).withAlpha (0.96f), r.getX() + r.getWidth() * 0.35f, r.getBottom(), false);
    g.setGradientFill (membrane);
    g.fillRoundedRectangle (r, 9.0f);

    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 9.0f, r.getRight() - 9.0f);

    g.setColour (highlighted ? colours::vein : colours::vein.withAlpha (0.17f));
    g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, highlighted ? 1.6f : 1.0f);
}

void paintTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, juce::Colour colour)
{
    const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) area.getX() + 3.0f, (float) area.getCentreY() });
    g.setColour (colour.withAlpha (0.25f));
    g.fillEllipse (dot.expanded (3.0f));
    g.setColour (colour);
    g.fillEllipse (dot);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold).withKerningFactor (0.14f));
    g.drawText (title, area.withTrimmedLeft (12), juce::Justification::centredLeft);
}

void paintCicada (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto t = juce::AffineTransform::scale (area.getWidth() / 40.0f, area.getHeight() / 40.0f).translated (area.getX(), area.getY());

    juce::Path leftWing, rightWing, veins;
    leftWing.startNewSubPath (18.5f, 13.0f);
    leftWing.cubicTo (11.0f, 13.5f, 4.0f, 21.0f, 5.0f, 34.0f);
    leftWing.cubicTo (10.0f, 35.0f, 16.5f, 28.0f, 19.0f, 18.0f);
    leftWing.closeSubPath();
    rightWing = leftWing;
    rightWing.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f).translated (40.0f, 0.0f));

    for (const auto& seg : { std::array<float, 4> { 18.0f, 16.0f, 9.0f, 30.0f }, { 16.5f, 15.0f, 7.0f, 24.0f }, { 18.6f, 20.0f, 13.0f, 32.0f } })
    {
        veins.addLineSegment ({ seg[0], seg[1], seg[2], seg[3] }, 0.6f);
        veins.addLineSegment ({ 40.0f - seg[0], seg[1], 40.0f - seg[2], seg[3] }, 0.6f);
    }

    for (auto* wing : { &leftWing, &rightWing })
    {
        wing->applyTransform (t);
        g.setGradientFill (juce::ColourGradient (colours::teal.withAlpha (0.5f), area.getTopLeft(),
                                                 colours::violet.withAlpha (0.45f), area.getBottomRight(), false));
        g.fillPath (*wing);
        g.setColour (colours::vein);
        g.strokePath (*wing, juce::PathStrokeType (0.8f));
    }

    veins.applyTransform (t);
    g.setColour (colours::vein.withAlpha (0.8f));
    g.fillPath (veins);

    const auto scaled = [&] (float x, float y, float w, float h) { return juce::Rectangle<float> (x, y, w, h).transformedBy (t); };
    g.setColour (colours::emblemBody);
    g.fillEllipse (scaled (16.6f, 13.0f, 6.8f, 18.0f));
    g.setColour (colours::emblemHead);
    g.fillEllipse (scaled (14.8f, 8.5f, 10.4f, 6.0f));
    g.setColour (colours::eye);
    g.fillEllipse (scaled (13.4f, 9.3f, 3.8f, 3.8f));
    g.fillEllipse (scaled (22.8f, 9.3f, 3.8f, 3.8f));
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::bg);
    setColour (juce::Label::textColourId, colours::ink);
    setColour (juce::Slider::textBoxTextColourId, colours::ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colours::gold.withAlpha (0.4f));
    setColour (juce::ComboBox::backgroundColourId, colours::well);
    setColour (juce::TooltipWindow::backgroundColourId, colours::well);
    setColour (juce::TooltipWindow::textColourId, colours::ink);
    setColour (juce::TooltipWindow::outlineColourId, colours::vein.withAlpha (0.4f));
    setColour (juce::ScrollBar::thumbColourId, colours::sage.withAlpha (0.6f));
    setColour (juce::ComboBox::textColourId, colours::ink);
    setColour (juce::ComboBox::outlineColourId, colours::line);
    setColour (juce::ComboBox::arrowColourId, colours::sage);
    setColour (juce::PopupMenu::backgroundColourId, colours::well);
    setColour (juce::PopupMenu::textColourId, colours::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::vein.withAlpha (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::ink);
    setColour (juce::TextEditor::backgroundColourId, colours::bg);
    setColour (juce::TextEditor::textColourId, colours::ink);
    setColour (juce::TextEditor::highlightColourId, colours::gold.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, colours::ink);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = arcThickness (radius);
    const float arcR = radius - lineW * 0.5f;

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float fromAngle = startAngle + zeroPos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::line);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    g.setColour (slider.isEnabled() ? accent : colours::sage);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Glossy body, like a compound eye: lighter at the top left, amber vein rim
    const float knobR = arcR - lineW * 1.8f;
    const auto body = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (colours::knob.brighter (0.45f), centre.translated (-knobR * 0.5f, -knobR * 0.6f),
                                             colours::knob.darker (0.3f), centre.translated (knobR * 0.6f, knobR * 0.7f), true));
    g.fillEllipse (body);
    g.setColour (colours::vein.withAlpha (0.3f));
    g.drawEllipse (body, 0.8f);

    const auto tip = centre.getPointOnCircumference (knobR * 0.82f, angle);
    const auto mid = centre.getPointOnCircumference (knobR * 0.38f, angle);
    g.setColour (colours::ink);
    g.drawLine ({ mid, tip }, 1.8f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();

    if (on)
    {
        g.setColour (accent.withAlpha (0.12f));
        g.fillRoundedRectangle (r.expanded (1.0f), r.getHeight() * 0.5f + 1.0f);
    }

    g.setColour (on ? accent.withAlpha (0.2f) : colours::well);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? accent : (highlighted ? colours::sage : colours::line));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    g.setColour (on ? colours::ink : colours::sage);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold).withKerningFactor (0.1f));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::vein : colours::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 1.5f, ax + 3.5f, ay - 1.5f, ax, ay + 2.5f);
    g.setColour (colours::sage);
    g.fillPath (arrow);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    g.setColour (down ? accent.withAlpha (0.3f) : (highlighted ? colours::membrane.brighter (0.15f) : colours::well));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (highlighted || down ? accent : colours::line);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.1f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int) { return juce::FontOptions (10.0f, juce::Font::bold).withKerningFactor (0.08f); }

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return juce::FontOptions (11.5f); }

juce::Font LookAndFeel::getLabelFont (juce::Label& l)
{
    // Slider value boxes use a small figure size
    if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
        return juce::FontOptions (10.5f);

    return l.getFont();
}

//==============================================================================
void paintVenation (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (colours::bg);
    g.fillRect (area);

    juce::Path tile;
    const float pts[][4] { { 0, 52, 38, 40 }, { 38, 40, 71, 58 }, { 71, 58, 104, 30 }, { 104, 30, 140, 47 }, { 140, 47, 180, 38 },
                           { 38, 40, 33, 0 }, { 104, 30, 112, 0 }, { 71, 58, 77, 99 }, { 77, 99, 46, 121 }, { 46, 121, 0, 112 },
                           { 77, 99, 118, 88 }, { 118, 88, 140, 47 }, { 118, 88, 150, 108 }, { 150, 108, 180, 96 },
                           { 46, 121, 52, 140 }, { 150, 108, 156, 140 }, { 140, 47, 162, 0 } };

    for (const auto& p : pts)
        tile.addLineSegment ({ p[0], p[1], p[2], p[3] }, 1.0f);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    g.setColour (colours::vein.withAlpha (0.05f));

    for (int y = area.getY(); y < area.getBottom(); y += 140)
        for (int x = area.getX(); x < area.getRight(); x += 180)
            g.fillPath (tile, juce::AffineTransform::translation ((float) x, (float) y));
}

void paintHeader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& productName, const juce::String& tagline)
{
    auto row = area;
    paintCicada (g, row.removeFromLeft (28).withSizeKeepingCentre (28, 28).toFloat());
    row.removeFromLeft (8);

    const auto wordmarkFont = juce::Font (juce::FontOptions (17.0f, juce::Font::bold).withKerningFactor (0.12f));
    g.setColour (colours::ink);
    g.setFont (wordmarkFont);
    g.drawText (productName.toUpperCase(), row, juce::Justification::centredLeft);

    const int wordmarkWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (wordmarkFont, productName.toUpperCase()));
    g.setColour (colours::sage);
    g.setFont (juce::FontOptions (10.5f).withKerningFactor (0.04f));
    g.drawText (tagline, row.withTrimmedLeft (wordmarkWidth + 16), juce::Justification::centredLeft);

    // Iridescent hairline: teal, chlorophyll, gold, violet
    const auto y = (float) area.getBottom() - 1.0f;
    juce::ColourGradient sheen (colours::teal, (float) area.getX(), y, colours::violet, (float) area.getRight(), y, false);
    sheen.addColour (0.35, colours::chlorophyll);
    sheen.addColour (0.7, colours::gold);
    g.setGradientFill (sheen);
    g.setOpacity (0.45f);
    g.fillRect ((float) area.getX(), y, (float) area.getWidth(), 1.0f);
    g.setOpacity (1.0f);
}

juce::Colour spectrogramColour (float v)
{
    struct Stop { float pos; juce::uint32 argb; };
    static constexpr Stop stops[] = { { 0.00f, 0xff050704 }, { 0.28f, 0xff1e2c12 }, { 0.52f, 0xff267054 },
                                      { 0.76f, 0xffd6a040 }, { 1.00f, 0xfffaf4d2 } };
    v = juce::jlimit (0.0f, 1.0f, v);

    for (size_t i = 1; i < std::size (stops); ++i)
        if (v <= stops[i].pos)
            return juce::Colour (stops[i - 1].argb).interpolatedWith (juce::Colour (stops[i].argb),
                                                                      (v - stops[i - 1].pos) / (stops[i].pos - stops[i - 1].pos));

    return juce::Colour (stops[std::size (stops) - 1].argb);
}

} // namespace cicada

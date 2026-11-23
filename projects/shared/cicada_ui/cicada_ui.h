/*
  ==============================================================================

   Cicada: the shared look of our audio plug-ins.

   Night-foliage ground, smoky wing-membrane panels with amber venation, and
   accents assigned by role (see the "Cicada" design system for the rules).

  ==============================================================================

  BEGIN_JUCE_MODULE_DECLARATION

   ID:                 cicada_ui
   vendor:             cicada
   version:            1.0.0
   name:               Cicada plug-in UI theme
   description:        Look-and-feel, palette and painting helpers for the Cicada plug-in family
   license:            AGPLv3/Commercial (follows the JUCE licence of the host project)
   minimumCppStandard: 17

   dependencies:       juce_gui_basics

  END_JUCE_MODULE_DECLARATION

  ==============================================================================
*/

#pragma once
#define CICADA_UI_H_INCLUDED

#include <juce_gui_basics/juce_gui_basics.h>

namespace cicada
{

/** The palette. Names match the design system's colour tokens. */
namespace colours
{
    inline const juce::Colour bg          { 0xff0a0c08 };  // window ground, night foliage
    inline const juce::Colour well        { 0xff060805 };  // recessed displays, menus, idle toggles
    inline const juce::Colour membrane    { 0xff161b11 };  // panel base
    inline const juce::Colour membraneHi  { 0xff222a19 };  // panel gradient top-left, hover
    inline const juce::Colour line        { 0xff283022 };  // dark venation: tracks, grids, outlines (decorative)
    inline const juce::Colour vein        { 0xffc9a25a };  // amber vein: panel edges, focus, rims
    inline const juce::Colour knob        { 0xff171c12 };  // knob body
    inline const juce::Colour ink         { 0xffecead9 };  // primary text
    inline const juce::Colour sage        { 0xff8b9078 };  // secondary text

    // Accents, assigned by role
    inline const juce::Colour eye         { 0xffff5b3a };  // primary signal source; danger
    inline const juce::Colour teal        { 0xff4fd6c4 };  // secondary source / sidechain
    inline const juce::Colour gold        { 0xffd8b25c };  // processing sections
    inline const juce::Colour chlorophyll { 0xffa6e05a };  // modulation source 1
    inline const juce::Colour violet      { 0xffc79bff };  // modulation source 2

    inline const juce::Colour emblemBody  { 0xff2a3a1b };
    inline const juce::Colour emblemHead  { 0xff33461f };

    /** Accent of modulation source n (0 = chlorophyll, 1 = violet, then they repeat). */
    inline juce::Colour modSource (int index) { return (index % 2) == 0 ? chlorophyll : violet; }
}

/** Smoky translucent panel with an amber vein edge; highlighted when something can be dropped on it. */
void paintMembrane (juce::Graphics&, juce::Rectangle<float>, bool highlighted = false);

/** Section title (uppercase, tracked) led by a glowing dot in the section's accent. */
void paintTitle (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title, juce::Colour accent);

/** The cicada emblem, scaled into a square area. */
void paintCicada (juce::Graphics&, juce::Rectangle<float> area);

/** Fills the area with the ground colour and the faint wing-venation texture. */
void paintVenation (juce::Graphics&, juce::Rectangle<int> area);

/** Header row: emblem, wordmark, tagline and the iridescent hairline along its bottom edge. */
void paintHeader (juce::Graphics&, juce::Rectangle<int> area, const juce::String& productName, const juce::String& tagline);

/** Spectrogram colour map (olive, jade, amber, pale membrane) for a level from 0 to 1. */
juce::Colour spectrogramColour (float level);

//==============================================================================
/** Slim glossy knobs, glowing pill toggles, vein-edged menus and buttons, compact type.

    Colour a rotary slider's arc with Slider::rotarySliderFillColourId and a toggle's
    "on" state with ToggleButton::tickColourId (set them to the section's accent).
*/
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;

    /** Geometry used by drawRotarySlider, so components can draw modulation rings that line up. */
    static float arcThickness (float radius) noexcept { return juce::jmax (2.0f, radius * 0.11f); }
};

} // namespace cicada

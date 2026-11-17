/*
  ==============================================================================

   Detail Clipper editor, in the Cicada look (projects/shared/cicada_ui).

   Accent roles: the three processing sections (Clip, Shape, Detail) are gold,
   Output is ink, and the input signal (the primary source) is eye wherever it
   is drawn.

  ==============================================================================
*/

#pragma once

#include <cicada_ui/cicada_ui.h>
#include "PluginProcessor.h"

namespace detail_clipper
{

//==============================================================================
/** Caption above, Cicada rotary knob, value below. Double-click resets. */
class KnobCell final : public juce::Component
{
public:
    KnobCell (juce::AudioProcessorValueTreeState&, const char* paramID, const juce::String& caption, juce::Colour accent);

    void resized() override;

private:
    juce::Label caption;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
class DetailClipperEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit DetailClipperEditor (DetailClipperAudioProcessor&);
    ~DetailClipperEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum Section { clipSection, shapeSection, detailSection, outputSection, numSections };

    struct Panel
    {
        juce::String title;
        juce::Colour accent;
        juce::Rectangle<int> bounds;
        juce::TextButton reset { "Reset" };
        double flashStart = -1.0;
    };

    void timerCallback() override;
    void resetSection (int section);
    void flash (int section);

    void drawTransferCurve (juce::Graphics&, juce::Rectangle<float>) const;
    void drawScope (juce::Graphics&, juce::Rectangle<float>) const;
    void drawClipMeter (juce::Graphics&, juce::Rectangle<int>) const;

    juce::ToggleButton& addPill (juce::ToggleButton&, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>&,
                                 const char* paramID, juce::Colour accent, const juce::String& tooltip);
    juce::ComboBox& addCombo (juce::ComboBox&, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>&,
                              const char* paramID);

    DetailClipperAudioProcessor& processor;
    cicada::LookAndFeel lnf;

    std::array<Panel, numSections> panels;
    juce::TextButton resetAll { "Reset all" };

    KnobCell drive, ceiling, knee, asymmetry, detail, detailFreq, detailHeadroom, output, mix;

    juce::ComboBox oversamplingBox, stereoBox;
    juce::Label oversamplingCaption, stereoCaption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment, stereoAttachment;

    juce::ToggleButton adaaPill { "ADAA" }, trueCeilingPill { "TRUE CEILING" }, autoGainPill { "AUTO GAIN" }, deltaPill { "DELTA" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> adaaAttachment, trueCeilingAttachment,
                                                                         autoGainAttachment, deltaAttachment;

    juce::Rectangle<int> curveWell, scopeWell, clipMeterArea, clipHelpArea, ioArea, detailHelpArea;

    // scope history, newest at the end
    static constexpr int scopeLength = 2048;
    std::vector<float> scopeDriven = std::vector<float> (scopeLength, 0.0f);
    std::vector<float> scopeOutput = std::vector<float> (scopeLength, 0.0f);

    float displayedClipDb = 0.0f, displayedIn = 0.0f, displayedOut = 0.0f;

    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DetailClipperEditor)
};

} // namespace detail_clipper

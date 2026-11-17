#pragma once

#include "PluginProcessor.h"

namespace ui
{
    namespace colours
    {
        const juce::Colour background { 0xff0e0d14 };
        const juce::Colour panel      { 0xff17151f };
        const juce::Colour outline    { 0xff2a2736 };
        const juce::Colour text       { 0xffe8e6f0 };
        const juce::Colour dim        { 0xff8a86a0 };
        const juce::Colour accentA    { 0xffff7a59 };
        const juce::Colour accentB    { 0xff57c7ff };
        const juce::Colour accentFx   { 0xffb388ff };
    }

    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getLabelFont (juce::Label&) override;
    };

    /** A rotary knob with a caption above and its value below. */
    class Knob final : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
              const juce::String& caption, juce::Colour accent);

        void resized() override;

    private:
        juce::Slider slider;
        juce::Label  label;
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    };

    /** A captioned combo box bound to a choice parameter. */
    class Choice final : public juce::Component
    {
    public:
        Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                const juce::String& caption, const juce::StringArray& items);

        void resized() override;

    private:
        juce::ComboBox box;
        juce::Label    label;
        juce::AudioProcessorValueTreeState::ComboBoxAttachment attachment;
    };

    class Toggle final : public juce::Component
    {
    public:
        Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                const juce::String& caption, juce::Colour accent);

        void resized() override { button.setBounds (getLocalBounds()); }

    private:
        juce::ToggleButton button;
        juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
    };

    /** Displays a slot's spectral image, accepts dropped files and shows the play heads. */
    class SpectrogramView final : public juce::Component,
                                  public juce::FileDragAndDropTarget,
                                  private juce::Timer
    {
    public:
        SpectrogramView (SpectralWeaverProcessor&, int slot, juce::String title, juce::Colour accent);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true;  repaint(); }
        void fileDragExit (const juce::StringArray&) override             { dragOver = false; repaint(); }
        void filesDropped (const juce::StringArray& files, int, int) override;

        void refresh();

    private:
        void timerCallback() override;
        void browse();
        juce::Rectangle<float> imageArea() const;

        SpectralWeaverProcessor& processor;
        const int slot;
        const juce::String title;
        const juce::Colour accent;

        sw::SpectralImage::Ptr image;
        std::unique_ptr<juce::FileChooser> chooser;
        bool dragOver = false;
        std::array<float, SpectralWeaverProcessor::numVoices> lastPositions {};
    };
}

//==============================================================================
class SpectralWeaverEditor final : public juce::AudioProcessorEditor,
                                   private juce::ChangeListener
{
public:
    explicit SpectralWeaverEditor (SpectralWeaverProcessor&);
    ~SpectralWeaverEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void paintPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour accent) const;

    template <typename... Args>
    ui::Knob& knob (Args&&... args)
    {
        auto& k = *knobs.emplace_back (std::make_unique<ui::Knob> (std::forward<Args> (args)...));
        addAndMakeVisible (k);
        return k;
    }

    SpectralWeaverProcessor& processor;
    ui::LookAndFeel lnf;

    ui::SpectrogramView viewA, viewB;
    std::vector<std::unique_ptr<ui::Knob>> knobs;

    // Ordered groups for layout
    std::vector<juce::Component*> playbackKnobs, crossKnobs, spectralKnobs, lfoKnobs, ampKnobs;
    ui::Toggle freeze, loop, keyTrack, syncB;
    ui::Choice crossMode, lfoShape, lfoTarget;
    juce::MidiKeyboardComponent keyboard;

    juce::Rectangle<int> playbackPanel, crossPanel, spectralPanel, lfoPanel, ampPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralWeaverEditor)
};

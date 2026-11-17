#pragma once

#include "PluginProcessor.h"
#include <cicada_ui/cicada_ui.h>

namespace ui
{
    // The Cicada theme comes from the shared cicada_ui module; these are this editor's role names for it.
    namespace colours
    {
        const juce::Colour background = cicada::colours::bg;
        const juce::Colour well       = cicada::colours::well;
        const juce::Colour panel      = cicada::colours::membrane;
        const juce::Colour outline    = cicada::colours::line;
        const juce::Colour vein       = cicada::colours::vein;
        const juce::Colour knob       = cicada::colours::knob;
        const juce::Colour text       = cicada::colours::ink;
        const juce::Colour dim        = cicada::colours::sage;
        const juce::Colour accentA    = cicada::colours::eye;          // sample A
        const juce::Colour accentB    = cicada::colours::teal;         // sample B
        const juce::Colour accentFx   = cicada::colours::gold;         // spectral sculpting
        const juce::Colour lfo1       = cicada::colours::chlorophyll;
        const juce::Colour lfo2       = cicada::colours::violet;
        inline juce::Colour lfo (int index) { return cicada::colours::modSource (index); }
    }

    using cicada::paintMembrane;
    using cicada::paintTitle;
    using cicada::paintCicada;
    using LookAndFeel = cicada::LookAndFeel;

    /** A rotary knob with a caption above and its value below. Knobs that are modulation
        targets accept a dragged LFO and draw each LFO's range plus the live value. */
    class Knob final : public juce::Component,
                       public juce::DragAndDropTarget
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
              const juce::String& caption, juce::Colour accent);

        void resized() override;
        void paintOverChildren (juce::Graphics&) override;

        void enableModulation (SpectralWeaverProcessor&, int targetIndex);
        bool needsLiveRepaint() const;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        static int lfoFromDescription (const juce::var& description);

        juce::String paramId;
        juce::Slider slider;
        juce::Label  label;
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;

        SpectralWeaverProcessor* processor = nullptr;
        int modIndex = -1;
        int dragOverLfo = -1;
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
        void setTooltip (const juce::String& t) { button.setTooltip (t); }

    private:
        juce::ToggleButton button;
        juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
    };

    //==============================================================================
    /** Shows an LFO's shape and live position; drawing on it creates a custom shape. */
    class LfoDisplay final : public juce::Component
    {
    public:
        LfoDisplay (SpectralWeaverProcessor&, int lfo);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override       { dragIndex = -1; }
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> plot() const              { return getLocalBounds().toFloat().reduced (9.0f); }
        juce::Point<float> toModel (const juce::MouseEvent&) const;
        juce::Point<float> toScreen (juce::Point<float> model) const;
        int nearestPoint (juce::Point<float> screenPos) const;
        int currentShape() const;
        void commit (juce::Point<float> tracked);

        SpectralWeaverProcessor& processor;
        const int lfo;
        sw::ShapePoints editing;
        int dragIndex = -1;
    };

    /** The pill you drag onto a knob to route an LFO to it. */
    class LfoDragHandle final : public juce::Component,
                                public juce::SettableTooltipClient
    {
    public:
        explicit LfoDragHandle (int lfoIndex) : lfo (lfoIndex)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            setTooltip ("Drag onto any knob to modulate it");
        }

        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        const int lfo;
    };

    /** One routed target: its name, a depth slider and a remove button. */
    class RouteRow final : public juce::Component
    {
    public:
        RouteRow (SpectralWeaverProcessor&, int lfo, const juce::String& targetId);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        juce::Label name;
        juce::Slider depth;
        juce::TextButton remove { "x" };
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
        juce::Colour colour;
    };

    /** Everything for one LFO: shape editor, settings and its target list. */
    class LfoPanel final : public juce::Component,
                           private juce::ValueTree::Listener,
                           private juce::AsyncUpdater
    {
    public:
        LfoPanel (SpectralWeaverProcessor&, int lfo);
        ~LfoPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Called from the editor's timer. */
        void refresh();

    private:
        void handleAsyncUpdate() override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { triggerAsyncUpdate(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
        void valueTreeRedirected (juce::ValueTree&) override                          { triggerAsyncUpdate(); }
        void layoutRoutes();

        SpectralWeaverProcessor& processor;
        const int lfo;
        const juce::Colour colour;

        LfoDragHandle handle;
        juce::TextButton reset { "RESET" };
        LfoDisplay display;
        juce::Label hint;
        std::unique_ptr<Choice> shape, trigger, division;
        std::unique_ptr<Toggle> sync, unipolar;
        std::unique_ptr<Knob> rate, phase, smooth, fade;

        juce::Viewport routesView;
        juce::Component routesContent;
        juce::Label emptyRoutes;
        std::vector<std::unique_ptr<RouteRow>> rows;
        juce::ComboBox addTarget;
        juce::Rectangle<int> routesArea;
    };

    /** Displays a slot's spectral image, accepts dropped files and shows the play heads. */
    class SpectrogramView final : public juce::Component,
                                  public juce::FileDragAndDropTarget,
                                  private juce::Timer
    {
    public:
        SpectrogramView (SpectralWeaverProcessor&, int slot, juce::String title, juce::Colour accent);

        void paint (juce::Graphics&) override;
        void resized() override;
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

        Toggle normalise;
        juce::TextButton swap { "SWAP A <> B" };
        juce::Rectangle<float> statusArea;

        sw::SpectralImage::Ptr image;
        std::unique_ptr<juce::FileChooser> chooser;
        bool dragOver = false;
        std::array<float, SpectralWeaverProcessor::numVoices> lastPositions {};
    };
}

//==============================================================================
class SpectralWeaverEditor final : public juce::AudioProcessorEditor,
                                   public juce::DragAndDropContainer,
                                   private juce::ChangeListener,
                                   private juce::Timer
{
public:
    explicit SpectralWeaverEditor (SpectralWeaverProcessor&);
    ~SpectralWeaverEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
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
    std::vector<juce::Component*> playbackKnobs, crossKnobs, spectralKnobs, ampKnobs;
    ui::Toggle freeze, loop, keyTrack, syncB;
    ui::Choice crossMode;
    std::array<std::unique_ptr<ui::LfoPanel>, sw::numLfos> lfoPanels;
    juce::MidiKeyboardComponent keyboard;

    // Section reset buttons, in the same order as SpectralWeaverProcessor's parameter groups
    static constexpr const char* resetGroupIds[] { "playback", "cross", "spectral", "amp" };
    std::array<juce::TextButton, 4> resetButtons;
    juce::TextButton resetAllButton { "RESET ALL" };
    juce::TooltipWindow tooltips { this, 600 };

    juce::Rectangle<int> playbackPanel, crossPanel, spectralPanel, ampPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralWeaverEditor)
};

#pragma once

#include "PluginProcessor.h"
#include <cicada_ui/cicada_ui.h>

namespace ui
{
    /** Cicada roles for this plug-in: A is the primary source, B the second source,
        Transfer is processing and Output is neutral. */
    namespace roles
    {
        const juce::Colour performance = cicada::colours::eye;
        const juce::Colour timbre      = cicada::colours::teal;
        const juce::Colour transfer    = cicada::colours::gold;
        const juce::Colour output      = cicada::colours::ink;
    }

    juce::Font labelFont();     // control-label: 9.5px bold, tracked
    juce::Font monoFont();      // value / status: 10.5px mono

    /** Rotary knob with its caption above and value below. Double-click resets it. */
    class Knob final : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, juce::Colour accent);
        void resized() override;

    private:
        juce::Slider slider;
        juce::Label label;
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    };

    /** A caption above any control (a pill or a menu), sized like a knob cell. */
    class Cell final : public juce::Component
    {
    public:
        Cell (const juce::String& caption, juce::Component& content, int contentHeight = 22);
        void resized() override;

    private:
        juce::Label label;
        juce::Component& content;
        int contentHeight;
    };

    /** Recessed display of the live input: loudness, detected pitch and the pitch being played. */
    class InputView final : public juce::Component
    {
    public:
        explicit InputView (tt::TransferEngine& e) : engine (e) {}
        void paint (juce::Graphics&) override;

    private:
        tt::TransferEngine& engine;
    };

    /** B's waveform and pitch track. */
    class TimbreView final : public juce::Component
    {
    public:
        void setModel (const tt::TimbreModel*);
        void paint (juce::Graphics&) override;

    private:
        const tt::TimbreModel* model = nullptr;
        std::vector<std::pair<float, float>> peaks;
    };

    /** What was learned from B: the harmonic envelope at three loudness levels and the noise floor. */
    class FingerprintView final : public juce::Component
    {
    public:
        void setModel (const tt::TimbreModel* m) { model = m; repaint(); }
        void paint (juce::Graphics&) override;

    private:
        const tt::TimbreModel* model = nullptr;
    };

    /** Scrolling log-frequency spectrogram of the output, in the Cicada spectrogram ramp. */
    class SpectrogramView final : public juce::Component
    {
    public:
        explicit SpectrogramView (tt::TransferEngine&);
        void update();
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        tt::TransferEngine& engine;
        tt::Fft fft { 10 };
        std::vector<double> re, im, window;
        std::vector<float> pending;
        int lastRead = 0;
        juce::Image image;
    };
}

//==============================================================================
class TimbreTransferEditor final : public juce::AudioProcessorEditor,
                                   public juce::FileDragAndDropTarget,
                                   private juce::Timer,
                                   private juce::ChangeListener
{
public:
    explicit TimbreTransferEditor (TimbreTransferProcessor&);
    ~TimbreTransferEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshTimbre();
    void refreshMode();
    void layoutTransferControls();
    void browseForTimbre();
    void flash (juce::Rectangle<int>);

    TimbreTransferProcessor& processor;
    cicada::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Rectangle<int> panelA, panelB, panelT, panelO, flashArea;
    float flashAlpha = 0.0f;
    bool dragOver = false;
    int shownMode = -1;

    // A
    ui::InputView inputView;
    juce::Label inputStatus;

    // B
    juce::OwnedArray<juce::ToggleButton> presetPills;
    juce::TextButton loadButton { "LOAD FILE" }, resetB { "RESET" };
    ui::TimbreView timbreView;
    ui::FingerprintView fingerprint;
    juce::Label timbreStatus;
    std::unique_ptr<juce::FileChooser> chooser;
    const tt::TimbreModel* shownModel = nullptr;

    // Transfer
    juce::OwnedArray<juce::ToggleButton> modePills;
    juce::TextButton resetT { "RESET" };
    juce::Label modeHelp;
    juce::ToggleButton octaveToggle { "MATCH B" };
    juce::ComboBox grainBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> octaveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> grainAttachment;
    std::unique_ptr<ui::Cell> octaveCell, grainCell;
    std::unique_ptr<ui::Knob> transpose, snap, brightness, noise, consonants, vibrato, glide, continuity, pitchFix, detail, flatten, air;
    struct Slot { juce::Component* c; int modes; };   // modes: bit 0 harmonic, 1 mosaic, 2 vocoder
    std::vector<Slot> transferSlots;

    // Output
    ui::SpectrogramView spectrogram;
    juce::Label outputStatus;
    juce::TextButton resetO { "RESET" }, resetAll { "RESET ALL" };
    std::unique_ptr<ui::Knob> gate, mix, gain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimbreTransferEditor)
};

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "GridComponent.h"

namespace gf
{

/** Drag this into the DAW timeline / a MIDI track to drop the pattern as a .mid clip. */
class MidiDragPad final : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    explicit MidiDragPad (GrooveForgeProcessor& p) : processor (p)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag the pattern straight into your DAW as a MIDI clip");
    }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag (const juce::MouseEvent&) override;

private:
    GrooveForgeProcessor& processor;
    bool dragging = false;
};

//==============================================================================
class GrooveForgeEditor final : public juce::AudioProcessorEditor,
                                private juce::ChangeListener,
                                private juce::Timer
{
public:
    explicit GrooveForgeEditor (GrooveForgeProcessor&);
    ~GrooveForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshControls();
    void fitZoom();
    void applyTimeSignature();
    void exportMidi();
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& name, const juce::String& tooltip);

    GrooveForgeProcessor& processor;
    cicada::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::ComboBox genreBox;
    juce::TextButton generateButton { "GENERATE" }, playButton { "PLAY" }, exportButton { "EXPORT MIDI..." };
    juce::ToggleButton fillsToggle { "FILLS" }, splitToggle { "TRACK PER LAYER" }, followHostToggle { "FOLLOW HOST" },
                       midiOutToggle { "LIVE MIDI OUT" };

    juce::Label timeLabel { {}, "Time" }, slashLabel { {}, "/" }, groupLabel { {}, "Groups" }, barsLabel { {}, "Bars" },
                snapLabel { {}, "Snap" }, tempoLabel { {}, "BPM" }, zoomLabel { {}, "Zoom" };
    juce::ComboBox numeratorBox, denominatorBox, barsBox, snapBox;
    juce::TextEditor groupingEditor;
    juce::Slider tempoSlider, zoomSlider;

    juce::Slider complexityKnob, swingKnob, humanizeKnob, variationKnob, volumeKnob;
    juce::Label complexityLabel, swingLabel, humanizeLabel, variationLabel, volumeLabel;

    juce::Label hostLabel, statusLabel, meterInfoLabel;
    MidiDragPad dragPad;

    GridComponent grid;
    juce::Viewport viewport;
    juce::Component headerColumn;
    std::vector<std::unique_ptr<LaneHeader>> headers;

    std::unique_ptr<juce::FileChooser> chooser;
    bool refreshing = false;
    bool autoFit = true;

    // membrane panels (Cicada): generator = gold, meter = teal, feel = chlorophyll, export = ink, sequencer = eye
    juce::Rectangle<int> headerArea, genPanel, meterPanel, feelPanel, outPanel, seqPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveForgeEditor)
};

} // namespace gf

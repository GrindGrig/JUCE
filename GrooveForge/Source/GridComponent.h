#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cicada_ui/cicada_ui.h>
#include "PluginProcessor.h"

namespace gf
{

/** Cicada palette (same values as cicada::colours; literals so static initialisation order never matters). */
namespace Colours
{
    const juce::Colour background   { 0xff0a0c08 };   // bg
    const juce::Colour well         { 0xff060805 };
    const juce::Colour panel        { 0xff161b11 };   // membrane
    const juce::Colour panelLight   { 0xff222a19 };   // membrane-hi
    const juce::Colour line         { 0xff283022 };
    const juce::Colour vein         { 0xffc9a25a };
    const juce::Colour text         { 0xffecead9 };   // ink
    const juce::Colour textDim      { 0xff8b9078 };   // sage
    const juce::Colour eye          { 0xffff5b3a };
    const juce::Colour teal         { 0xff4fd6c4 };
    const juce::Colour gold         { 0xffd8b25c };
    const juce::Colour chlorophyll  { 0xffa6e05a };
    const juce::Colour violet       { 0xffc79bff };
    const juce::Colour accent       = gold;
}

//==============================================================================
/**
    The sequencer grid. Looks like a step sequencer (one row per layer, beats
    shaded in their accent groups) but behaves like a piano roll: notes live at
    arbitrary positions, the snap value is only a helper.

    Mouse:  click empty cell = add note (then drag to place it)
            click a note without dragging = delete it (step-sequencer toggle)
            drag a note = move it (snapped); hold Alt/Option for free placement
            Shift + drag vertically = velocity;  mouse wheel over a note = velocity
            right-click = delete
    Keys:   Left/Right nudge the selected note by 1/128 (Shift: by the snap value)
            Up/Down change velocity, Delete/Backspace removes it,
            S switches a hand drum's stroke (tone/slap, doum/tek ...)
*/
class GridComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit GridComponent (GrooveForgeProcessor&);
    ~GridComponent() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void setPixelsPerBeat (float ppb);
    float getPixelsPerBeat() const     { return pixelsPerBeat; }
    int getLaneHeight() const          { return laneHeight; }
    void setLaneHeight (int h)         { laneHeight = h; updateSize(); }
    void updateSize();

    std::function<void (juce::String)> onStatus;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override;

    int noteAt (juce::Point<float> p) const;
    double xToBeat (float x) const     { return x / pixelsPerBeat; }
    float beatToX (double b) const     { return (float) (b * pixelsPerBeat); }
    int yToLane (float y) const        { return juce::jlimit (0, NumLanes - 1, (int) (y / (float) laneHeight)); }
    double snapBeat (double beat, bool free) const;
    juce::Rectangle<float> noteBounds (const DrumNote&) const;
    void showNoteStatus (int index);

    GrooveForgeProcessor& processor;
    float pixelsPerBeat = 120.0f;
    int laneHeight = 34;

    // drag state
    int dragIndex = -1;
    int selectedIndex = -1;
    double dragStartBeat = 0.0;
    float dragStartVelocity = 0.0f;
    bool dragMoved = false;
    bool dragCreated = false;
    double grabOffset = 0.0;
    float lastPlayheadX = -1.0f;
    int hoverIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridComponent)
};

//==============================================================================
/** The per-layer controls on the left of each row. */
class LaneHeader final : public juce::Component
{
public:
    LaneHeader (GrooveForgeProcessor&, int lane);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    GrooveForgeProcessor& processor;
    int lane;
    juce::TextButton nameButton, muteButton { "M" }, lockButton { "L" }, diceButton { "Gen" }, clearButton { "X" };
    juce::Slider noteSlider;
};

} // namespace gf

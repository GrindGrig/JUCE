#include "PluginEditor.h"
#include "MidiExport.h"

namespace gf
{

//==============================================================================
void MidiDragPad::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool over = isMouseOver (true);
    g.setColour (over ? Colours::accent.withAlpha (0.25f) : Colours::panelLight);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::accent);
    const float dashes[] = { 5.0f, 4.0f };
    juce::Path p;
    p.addRoundedRectangle (r.reduced (1.0f), 6.0f);
    juce::Path dashed;
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
    g.fillPath (dashed);

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawFittedText ("DRAG MIDI TO DAW", getLocalBounds().reduced (6), juce::Justification::centred, 2);
}

void MidiDragPad::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;

    dragging = true;
    const auto file = processor.writeTempMidiFile();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                 [this] { dragging = false; });
}

//==============================================================================
static void styleLabel (juce::Label& l, float size = 13.0f, bool dim = true)
{
    l.setFont (juce::FontOptions (size));
    l.setColour (juce::Label::textColourId, dim ? Colours::textDim : Colours::text);
    l.setJustificationType (juce::Justification::centredLeft);
}

void GrooveForgeEditor::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& name, const juce::String& tip)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRange (0.0, 1.0, 0.001);
    s.setTooltip (tip);
    s.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, Colours::panelLight);
    s.setColour (juce::Slider::thumbColourId, Colours::text);
    s.setPopupDisplayEnabled (true, true, this);
    addAndMakeVisible (s);

    l.setText (name, juce::dontSendNotification);
    styleLabel (l, 12.0f);
    l.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (l);
}

GrooveForgeEditor::GrooveForgeEditor (GrooveForgeProcessor& p)
    : AudioProcessorEditor (p), processor (p), dragPad (p), grid (p)
{
    lnf.setColourScheme ({ 0xff1e2128, 0xff15171c, 0xff1e2128, 0xff5a6070, 0xffe6e8ee,
                           0xff2a2e37, 0xffffffff, 0xffff8a3a, 0xffe6e8ee });
    setLookAndFeel (&lnf);

    title.setText ("GROOVE FORGE", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, Colours::accent);
    addAndMakeVisible (title);

    genreBox.addItemList (Generator::genreNames(), 1);
    genreBox.setTooltip ("Genre the generator writes in");
    genreBox.onChange = [this]
    {
        if (refreshing) return;
        auto& prm = processor.getParams();
        prm.genre = (Genre) (genreBox.getSelectedItemIndex());
        prm.swing = Generator::info (prm.genre).swing;
        if (! processor.hostIsPlaying.load())
            processor.tempo = Generator::info (prm.genre).tempo;
        processor.generate();
        refreshControls();
    };
    addAndMakeVisible (genreBox);

    generateButton.setColour (juce::TextButton::buttonColourId, Colours::accent);
    generateButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    generateButton.setTooltip ("Generate a new pattern for every unlocked layer");
    generateButton.onClick = [this] { processor.generate(); };
    addAndMakeVisible (generateButton);

    playButton.setClickingTogglesState (true);
    playButton.setTooltip ("Internal transport (when the host is playing, Groove Forge follows the host instead)");
    playButton.onClick = [this] { processor.internalPlaying = playButton.getToggleState(); };
    addAndMakeVisible (playButton);

    fillsToggle.setTooltip ("Put a fill at the end of the pattern (needs 2+ bars)");
    fillsToggle.onClick = [this] { if (! refreshing) { processor.getParams().fills = fillsToggle.getToggleState(); processor.generate (-1, false); } };
    addAndMakeVisible (fillsToggle);

    // --- meter row ---------------------------------------------------------
    for (auto* l : { &timeLabel, &slashLabel, &groupLabel, &barsLabel, &snapLabel, &tempoLabel, &zoomLabel })
    {
        styleLabel (*l);
        addAndMakeVisible (*l);
    }

    for (int n = 1; n <= 17; ++n)
        numeratorBox.addItem (juce::String (n), n);
    for (int d : { 2, 4, 8, 16 })
        denominatorBox.addItem (juce::String (d), d);
    numeratorBox.setTooltip ("Beats per bar");
    denominatorBox.setTooltip ("Beat unit");
    numeratorBox.onChange = [this] { if (! refreshing) { groupingEditor.setText ({}, false); applyTimeSignature(); } };
    denominatorBox.onChange = numeratorBox.onChange;
    addAndMakeVisible (numeratorBox);
    addAndMakeVisible (denominatorBox);

    groupingEditor.setTooltip ("How the bar is grouped, e.g. 2+2+3 for 7/8, 3+3+2 for 8/8, 3+2 for 5/4.\n"
                               "For x/8 and x/16 the numbers are 8ths/16ths per beat; for x/4 they group the beats.\n"
                               "Press Enter to apply.");
    groupingEditor.setInputRestrictions (24, "0123456789+");
    groupingEditor.onReturnKey = [this] { applyTimeSignature(); };
    groupingEditor.onFocusLost = [this] { if (groupingEditor.getText() != processor.getPattern().grouping) applyTimeSignature(); };
    addAndMakeVisible (groupingEditor);

    for (int b : { 1, 2, 4, 8, 16 })
        barsBox.addItem (juce::String (b), b);
    barsBox.setTooltip ("Pattern length in bars");
    barsBox.onChange = [this] { if (! refreshing) { processor.setBars (barsBox.getSelectedId()); grid.updateSize(); } };
    addAndMakeVisible (barsBox);

    for (size_t i = 0; i < snapModes().size(); ++i)
        snapBox.addItem (snapModes()[i].name, (int) i + 1);
    snapBox.setTooltip ("Grid snap for placing and moving notes. Use 'Off' or hold Alt to place notes anywhere.");
    snapBox.onChange = [this] { processor.snapIndex = snapBox.getSelectedItemIndex(); grid.repaint(); };
    addAndMakeVisible (snapBox);

    tempoSlider.setSliderStyle (juce::Slider::LinearBar);
    tempoSlider.setRange (40.0, 220.0, 0.1);
    tempoSlider.setTextValueSuffix (" bpm");
    tempoSlider.setTooltip ("Tempo for the internal transport and the MIDI file (the host tempo is used while the host plays)");
    tempoSlider.onValueChange = [this] { processor.tempo = tempoSlider.getValue(); };
    addAndMakeVisible (tempoSlider);

    zoomSlider.setSliderStyle (juce::Slider::LinearBar);
    zoomSlider.setRange (30.0, 400.0, 1.0);
    zoomSlider.setTextValueSuffix (" px/beat");
    zoomSlider.setTooltip ("Horizontal zoom (Ctrl/Cmd + mouse wheel over the grid also zooms)");
    zoomSlider.onValueChange = [this] { autoFit = false; grid.setPixelsPerBeat ((float) zoomSlider.getValue()); };
    addAndMakeVisible (zoomSlider);

    followHostToggle.setTooltip ("Switch to the host's time signature automatically");
    followHostToggle.onClick = [this] { processor.followHostMeter = followHostToggle.getToggleState(); };
    addAndMakeVisible (followHostToggle);

    // --- knobs -------------------------------------------------------------
    setupKnob (complexityKnob, complexityLabel, "Complexity", "Density and busyness: ghost notes, syncopated kicks, extra percussion");
    setupKnob (swingKnob, swingLabel, "Swing", "50% = straight, 66% = triplet shuffle, 75% = hard swing");
    setupKnob (humanizeKnob, humanizeLabel, "Humanize", "Off-grid timing drift and velocity variation (plus the genre's own micro-timing feel)");
    setupKnob (variationKnob, variationLabel, "Variation", "How much the bars differ from each other");
    setupKnob (volumeKnob, volumeLabel, "Volume", "Output level of the built-in drum kit");
    swingKnob.setRange (0.5, 0.75, 0.001);

    auto regenOnRelease = [this] { processor.generate (-1, false); };
    complexityKnob.onValueChange = [this] { processor.getParams().complexity = (float) complexityKnob.getValue(); };
    swingKnob.onValueChange      = [this] { processor.getParams().swing = (float) swingKnob.getValue(); };
    humanizeKnob.onValueChange   = [this] { processor.getParams().humanize = (float) humanizeKnob.getValue(); };
    variationKnob.onValueChange  = [this] { processor.getParams().variation = (float) variationKnob.getValue(); };
    volumeKnob.onValueChange     = [this] { processor.outputGain = (float) volumeKnob.getValue(); };
    complexityKnob.onDragEnd = regenOnRelease;
    swingKnob.onDragEnd = regenOnRelease;
    humanizeKnob.onDragEnd = regenOnRelease;
    variationKnob.onDragEnd = regenOnRelease;
    swingKnob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    for (auto* k : { &complexityKnob, &humanizeKnob, &variationKnob, &volumeKnob })
        k->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };

    // --- export ------------------------------------------------------------
    exportButton.setTooltip ("Save the pattern as a Standard MIDI File");
    exportButton.onClick = [this] { exportMidi(); };
    addAndMakeVisible (exportButton);
    addAndMakeVisible (dragPad);

    splitToggle.setTooltip ("Export each drum layer as its own MIDI track");
    splitToggle.onClick = [this] { processor.splitTracksOnExport = splitToggle.getToggleState(); };
    addAndMakeVisible (splitToggle);

    midiOutToggle.setTooltip ("Send the pattern as MIDI to the host while playing (route it to another drum instrument or record it)");
    midiOutToggle.onClick = [this] { processor.sendMidiOut = midiOutToggle.getToggleState(); };
    addAndMakeVisible (midiOutToggle);

    for (auto* l : { &hostLabel, &statusLabel, &meterInfoLabel })
    {
        styleLabel (*l, 12.0f);
        addAndMakeVisible (*l);
    }

    // --- grid --------------------------------------------------------------
    grid.onStatus = [this] (juce::String s)
    {
        statusLabel.setText (s.isEmpty() ? juce::String ("Click: add/remove  |  Drag: move (Alt = free/off-grid)  |  Shift+drag or wheel: velocity  |  Arrows: nudge  |  Ctrl+wheel: zoom")
                                         : s, juce::dontSendNotification);
    };
    grid.onStatus ({});
    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    for (int l = 0; l < NumLanes; ++l)
    {
        headers.push_back (std::make_unique<LaneHeader> (processor, l));
        headerColumn.addAndMakeVisible (*headers.back());
    }
    addAndMakeVisible (headerColumn);

    processor.addChangeListener (this);
    refreshControls();

    setResizable (true, true);
    setResizeLimits (960, 600, 2400, 1400);
    setSize (1280, 780);
    startTimerHz (10);
}

GrooveForgeEditor::~GrooveForgeEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void GrooveForgeEditor::refreshControls()
{
    const juce::ScopedValueSetter<bool> svs (refreshing, true);
    const auto& pat = processor.getPattern();
    const auto& prm = processor.getParams();

    genreBox.setSelectedItemIndex ((int) prm.genre, juce::dontSendNotification);
    numeratorBox.setSelectedId (pat.numerator, juce::dontSendNotification);
    denominatorBox.setSelectedId (pat.denominator, juce::dontSendNotification);
    if (! groupingEditor.hasKeyboardFocus (true))
        groupingEditor.setText (pat.grouping, false);
    barsBox.setSelectedId (pat.bars, juce::dontSendNotification);
    snapBox.setSelectedItemIndex (processor.snapIndex, juce::dontSendNotification);
    tempoSlider.setValue (processor.tempo, juce::dontSendNotification);

    complexityKnob.setValue (prm.complexity, juce::dontSendNotification);
    swingKnob.setValue (prm.swing, juce::dontSendNotification);
    humanizeKnob.setValue (prm.humanize, juce::dontSendNotification);
    variationKnob.setValue (prm.variation, juce::dontSendNotification);
    volumeKnob.setValue (processor.outputGain, juce::dontSendNotification);
    fillsToggle.setToggleState (prm.fills, juce::dontSendNotification);
    splitToggle.setToggleState (processor.splitTracksOnExport, juce::dontSendNotification);
    followHostToggle.setToggleState (processor.followHostMeter, juce::dontSendNotification);
    midiOutToggle.setToggleState (processor.sendMidiOut.load(), juce::dontSendNotification);

    const auto meter = Meter::create (pat.numerator, pat.denominator, pat.grouping);
    juce::String feel = pat.denominator >= 8 ? (meter.compound ? "compound" : "additive") : "simple";
    meterInfoLabel.setText (juce::String (pat.numerator) + "/" + juce::String (pat.denominator) + "  " + feel
                                + "  |  " + juce::String (meter.numTactus()) + " beats felt as " + pat.grouping
                                + "  |  " + juce::String (pat.notes.size()) + " notes",
                            juce::dontSendNotification);

    for (auto& h : headers)
        h->refresh();

    grid.updateSize();
    if (autoFit)
        fitZoom();
}

void GrooveForgeEditor::fitZoom()
{
    const double beats = processor.getPattern().lengthInBeats();
    const float fit = (float) ((viewport.getWidth() - 4) / juce::jmax (1.0, beats));
    if (fit > 1.0f)
    {
        grid.setPixelsPerBeat (juce::jmax (40.0f, fit));
        zoomSlider.setValue (grid.getPixelsPerBeat(), juce::dontSendNotification);
    }
}

void GrooveForgeEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshControls();
}

void GrooveForgeEditor::applyTimeSignature()
{
    const int num = juce::jmax (1, numeratorBox.getSelectedId());
    const int den = juce::jmax (2, denominatorBox.getSelectedId());
    processor.setTimeSignature (num, den, groupingEditor.getText());
    groupingEditor.setText (processor.getPattern().grouping, false);
    refreshControls();
}

void GrooveForgeEditor::timerCallback()
{
    const bool hostPlaying = processor.hostIsPlaying.load();
    const int hn = processor.hostNumerator.load(), hd = processor.hostDenominator.load();
    const double bpm = processor.hostBpm.load();

    juce::String host;
    if (hostPlaying)
        host << "Following host transport";
    else if (processor.internalPlaying.load())
        host << "Internal transport";
    else
        host << "Stopped";
    if (hn > 0)
        host << "  |  host " << hn << "/" << hd << (bpm > 0 ? "  " + juce::String (bpm, 1) + " bpm" : juce::String());
    hostLabel.setText (host, juce::dontSendNotification);

    playButton.setButtonText (playButton.getToggleState() ? "Stop" : "Play");

    if (processor.followHostMeter && hn > 0 && hd > 0
        && (hn != processor.getPattern().numerator || hd != processor.getPattern().denominator)
        && (hd == 2 || hd == 4 || hd == 8 || hd == 16))
    {
        processor.setTimeSignature (hn, hd, {});
        refreshControls();
    }
}

void GrooveForgeEditor::exportMidi()
{
    chooser = std::make_unique<juce::FileChooser> ("Export MIDI",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                       .getChildFile (processor.suggestedFileName()),
                                                   "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("mid")) f = f.withFileExtension ("mid");
                              const bool ok = MidiExport::writeToFile (f, processor.getPattern(), processor.getLanes(),
                                                                       processor.tempo, processor.splitTracksOnExport);
                              statusLabel.setText (ok ? "Saved " + f.getFullPathName() : "Could not write " + f.getFullPathName(),
                                                   juce::dontSendNotification);
                          });
}

//==============================================================================
void GrooveForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    g.setColour (Colours::panel);
    g.fillRect (getLocalBounds().removeFromTop (156));
    g.setColour (juce::Colour (0xff0e1013));
    g.drawHorizontalLine (156, 0.0f, (float) getWidth());
}

void GrooveForgeEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (156).reduced (10, 6);

    // right block: export
    auto exportArea = top.removeFromRight (250);
    {
        auto a = exportArea;
        auto row = a.removeFromTop (28);
        exportButton.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (6);
        midiOutToggle.setBounds (row);
        a.removeFromTop (4);
        splitToggle.setBounds (a.removeFromBottom (22));
        dragPad.setBounds (a.reduced (0, 2));
    }
    top.removeFromRight (12);

    auto row1 = top.removeFromTop (34);
    title.setBounds (row1.removeFromLeft (170));
    genreBox.setBounds (row1.removeFromLeft (190).reduced (0, 3));
    row1.removeFromLeft (8);
    generateButton.setBounds (row1.removeFromLeft (120).reduced (0, 2));
    row1.removeFromLeft (8);
    fillsToggle.setBounds (row1.removeFromLeft (64));
    row1.removeFromLeft (8);
    playButton.setBounds (row1.removeFromLeft (64).reduced (0, 3));
    row1.removeFromLeft (8);
    tempoLabel.setBounds (row1.removeFromLeft (34));
    tempoSlider.setBounds (row1.removeFromLeft (120).reduced (0, 5));
    row1.removeFromLeft (8);
    hostLabel.setBounds (row1);

    top.removeFromTop (4);
    auto row2 = top.removeFromTop (30);
    timeLabel.setBounds (row2.removeFromLeft (36));
    numeratorBox.setBounds (row2.removeFromLeft (56).reduced (0, 2));
    slashLabel.setBounds (row2.removeFromLeft (14));
    denominatorBox.setBounds (row2.removeFromLeft (56).reduced (0, 2));
    row2.removeFromLeft (10);
    groupLabel.setBounds (row2.removeFromLeft (48));
    groupingEditor.setBounds (row2.removeFromLeft (90).reduced (0, 2));
    row2.removeFromLeft (10);
    barsLabel.setBounds (row2.removeFromLeft (34));
    barsBox.setBounds (row2.removeFromLeft (56).reduced (0, 2));
    row2.removeFromLeft (10);
    snapLabel.setBounds (row2.removeFromLeft (36));
    snapBox.setBounds (row2.removeFromLeft (100).reduced (0, 2));
    row2.removeFromLeft (10);
    followHostToggle.setBounds (row2.removeFromLeft (150));
    row2.removeFromLeft (6);
    zoomLabel.setBounds (row2.removeFromLeft (40));
    zoomSlider.setBounds (row2.removeFromLeft (120).reduced (0, 4));

    top.removeFromTop (2);
    auto row3 = top;
    const int knobW = 76;
    for (auto [k, l] : { std::pair<juce::Slider*, juce::Label*> { &complexityKnob, &complexityLabel },
                         { &swingKnob, &swingLabel }, { &humanizeKnob, &humanizeLabel },
                         { &variationKnob, &variationLabel }, { &volumeKnob, &volumeLabel } })
    {
        auto a = row3.removeFromLeft (knobW);
        l->setBounds (a.removeFromBottom (14));
        k->setBounds (a.reduced (12, 0));
    }
    row3.removeFromLeft (14);
    meterInfoLabel.setBounds (row3.removeFromTop (row3.getHeight() / 2 + 6));

    // body
    auto status = r.removeFromBottom (24).reduced (10, 2);
    statusLabel.setBounds (status);

    auto body = r.reduced (6, 4);
    const int laneH = juce::jmax (22, (body.getHeight() - 12) / NumLanes);
    grid.setLaneHeight (laneH);

    auto headerArea = body.removeFromLeft (300);
    headerColumn.setBounds (headerArea.withHeight (laneH * NumLanes));
    for (int l = 0; l < NumLanes; ++l)
        headers[(size_t) l]->setBounds (0, l * laneH, headerArea.getWidth(), laneH);

    viewport.setBounds (body.withHeight (laneH * NumLanes + viewport.getScrollBarThickness()));

    if (autoFit)
        fitZoom();
}

} // namespace gf

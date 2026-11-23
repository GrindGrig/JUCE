# cicada_ui

The shared Cicada look for our JUCE plug-ins: night-foliage ground, smoky
wing-membrane panels with amber venation, slim glossy knobs and glowing pill
toggles. Spectral Weaver (`projects/SpectralWeaver`) is the reference
implementation. The design rules (palette roles, type, density, components)
live in the **Cicada** design system.

## Add it to a plug-in

```cmake
juce_add_module("${CMAKE_CURRENT_SOURCE_DIR}/../shared/cicada_ui")

target_link_libraries(MyPlugin PRIVATE cicada_ui juce::juce_audio_utils)
```

```cpp
#include <cicada_ui/cicada_ui.h>

class MyEditor : public juce::AudioProcessorEditor
{
public:
    MyEditor (MyProcessor& p) : AudioProcessorEditor (p)
    {
        setLookAndFeel (&lnf);
        gain.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        gain.setColour (juce::Slider::rotarySliderFillColourId, cicada::colours::gold);  // section accent
        addAndMakeVisible (gain);
        setSize (640, 360);
    }

    ~MyEditor() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        cicada::paintVenation (g, getLocalBounds());
        cicada::paintHeader (g, getLocalBounds().removeFromTop (40).reduced (12, 0), "My Plugin", "what it does, in a few words");
        cicada::paintMembrane (g, panel.toFloat());
        cicada::paintTitle (g, panel.reduced (10, 5).removeFromTop (18), "Output", cicada::colours::ink);
    }

private:
    cicada::LookAndFeel lnf;
    juce::Slider gain;
    juce::Rectangle<int> panel { 12, 48, 200, 120 };
};
```

## What is in it

| Item | Purpose |
| --- | --- |
| `cicada::colours::*` | The palette, named like the design-system tokens (`bg`, `membrane`, `vein`, `ink`, `sage`, `eye`, `teal`, `gold`, `chlorophyll`, `violet`, ...). `modSource (n)` gives modulation source n's accent. |
| `cicada::LookAndFeel` | Rotary sliders, toggle pills, combo boxes, text buttons, compact fonts, tooltips, menus. |
| `paintVenation` | Window ground with the faint wing-vein texture. |
| `paintHeader` | Emblem, wordmark, tagline and the iridescent hairline. |
| `paintMembrane`, `paintTitle` | A section panel and its title with the glowing accent dot. |
| `paintCicada` | The emblem on its own. |
| `spectrogramColour` | The olive-jade-amber colour map for spectral displays. |

## Accent roles

Primary source `eye`, secondary source `teal`, processing `gold`, modulation
sources `chlorophyll` then `violet`, neutral sections `ink`. Colour is
reserved for meaning: a source's accent marks everything it touches.

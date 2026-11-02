#pragma once

#include "Theme.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace ui
{
/** A flat, rounded button painted from the current palette. */
class FlatButton : public juce::Button
{
public:
    enum class Kind
    {
        normal,   // outlined
        toggle,   // outlined; filled with `onColour` when toggled on
        danger,   // red text; filled red when toggled on
        ghost,    // dashed outline
        primary   // always filled with the accent colour
    };

    FlatButton (const juce::String& label = {}, Kind k = Kind::normal) : juce::Button (label), kind (k)
    {
        if (kind == Kind::toggle || kind == Kind::danger)
            setClickingTogglesState (true);
    }

    void setKind (Kind k)                  { kind = k; repaint(); }
    void setOnColour (juce::Colour c)      { onColour = c; hasOnColour = true; repaint(); }
    void setFontHeight (float h)           { fontHeight = h; repaint(); }
    void setIcon (Icon i)                  { icon = i; hasIcon = true; repaint(); }

    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    Kind kind;
    juce::Colour onColour;
    bool hasOnColour = false, hasIcon = false;
    Icon icon = Icon::plus;
    float fontHeight = 13.0f;
};

/** iOS-style on/off switch. */
class Switch : public juce::Button
{
public:
    Switch() : juce::Button ({}) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool, bool) override;
};

/** Peak meter with decay; push() a linear peak ~30 times a second. */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (bool verticalMeter = false) : vertical (verticalMeter) { setInterceptsMouseClicks (false, false); }

    void push (float linearPeak);
    void paint (juce::Graphics&) override;

private:
    bool vertical;
    float level = -60.0f;
};

/** A slider styled for the app (horizontal level or vertical fader). */
class LevelSlider : public juce::Slider
{
public:
    LevelSlider (bool vertical, double minDb, double maxDb, bool showText);
    void setTrackColour (juce::Colour c) { setColour (juce::Slider::trackColourId, c); }
};
} // namespace ui

#include "Widgets.h"

namespace ui
{
void FlatButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto& p = pal();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = juce::jmin (8.0f, r.getHeight() * 0.3f);
    const bool on = getToggleState();
    const auto accent = hasOnColour ? onColour : p.accent;

    juce::Colour fill = p.panel, border = p.border, textColour = p.text;

    switch (kind)
    {
        case Kind::normal:  break;
        case Kind::toggle:  if (on) { fill = accent; border = accent; textColour = juce::Colours::white; } else { textColour = p.dim; } break;
        case Kind::danger:  if (on) { fill = p.danger; border = p.danger; textColour = juce::Colours::white; } else { border = p.dangerBorder; textColour = p.danger; } break;
        case Kind::ghost:   fill = juce::Colours::transparentBlack; border = p.faint; textColour = p.dim; break;
        case Kind::primary: fill = accent; border = accent; textColour = juce::Colours::white; break;
    }

    if (! isEnabled())
        textColour = textColour.withAlpha (0.5f);

    if (isDown)            fill = fill.overlaidWith (p.text.withAlpha (0.08f));
    else if (isHighlighted) fill = fill.overlaidWith (p.text.withAlpha (0.04f));

    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);

    g.setColour (border);
    if (kind == Kind::ghost)
    {
        juce::Path outline;
        outline.addRoundedRectangle (r, radius);
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (outline, outline, dashes, 2);
        g.fillPath (outline);
    }
    else
    {
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    auto content = getLocalBounds().toFloat();
    g.setColour (textColour);
    g.setFont (font (fontHeight, true));

    if (hasIcon)
    {
        const float s = fontHeight + 3.0f;
        if (getButtonText().isEmpty())
        {
            drawIcon (g, icon, content.withSizeKeepingCentre (s, s), textColour);
            return;
        }

        const float textW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), getButtonText());
        const float total = s + 6.0f + textW;
        auto row = content.withSizeKeepingCentre (total, s);
        drawIcon (g, icon, row.removeFromLeft (s), textColour);
        row.removeFromLeft (6.0f);
        g.drawText (getButtonText(), row, juce::Justification::centredLeft, false);
        return;
    }

    g.drawFittedText (getButtonText(), getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
}

void Switch::paintButton (juce::Graphics& g, bool, bool)
{
    const auto& p = pal();
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (38.0f, 22.0f);
    const bool on = getToggleState();
    g.setColour (on ? p.green : p.faint);
    g.fillRoundedRectangle (r, 11.0f);
    const auto knob = juce::Rectangle<float> (16.0f, 16.0f).withPosition (on ? r.getRight() - 19.0f : r.getX() + 3.0f, r.getY() + 3.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (knob);
}

void LevelMeter::push (float linearPeak)
{
    const float db = juce::Decibels::gainToDecibels (linearPeak, -60.0f);
    const float next = db > level ? db : juce::jmax (db, level - 1.8f);
    if (std::abs (next - level) > 0.01f)
    {
        level = next;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto& p = pal();
    auto r = getLocalBounds().toFloat();
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    g.setColour (p.track);
    g.fillRoundedRectangle (r, radius);

    const float frac = juce::jlimit (0.0f, 1.0f, (level + 60.0f) / 60.0f);
    if (frac <= 0.0f)
        return;

    const auto colour = level > -3.0f ? p.danger : (level > -12.0f ? p.warn : p.green);
    g.setColour (colour);
    if (vertical)
        g.fillRoundedRectangle (r.withTop (r.getBottom() - r.getHeight() * frac), radius);
    else
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * frac), radius);
}

LevelSlider::LevelSlider (bool vertical, double minDb, double maxDb, bool showText)
{
    setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
    setTextBoxStyle (showText ? juce::Slider::TextBoxRight : juce::Slider::NoTextBox, false, 52, 20);
    setRange (minDb, maxDb, 0.5);
    setDoubleClickReturnValue (true, 0.0);
    textFromValueFunction = [minDb] (double v) { return v <= minDb ? juce::String ("off") : formatDb ((float) v); };
    valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-+.0123456789").getDoubleValue(); };
    setSkewFactorFromMidPoint (maxDb - 12.0);
    setPopupDisplayEnabled (! showText, false, nullptr);
    setVelocityBasedMode (false);
    updateText();
}
} // namespace ui

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace ui
{
struct Palette
{
    juce::Colour bg, panel, panel2, border, border2, text, dim, faint;
    juce::Colour nav, navSel, track, accent, wireIdle, grid;
    juce::Colour green, okBg, okFg, danger, dangerBorder, warn, stripSide;
    std::array<juce::Colour, 8> sources;
};

const Palette& pal();
bool isDark();
void setDark (bool dark);

juce::Font font (float height, bool bold = false);
juce::Font monoFont (float height);

/** Colour for the n-th source in display order. */
juce::Colour sourceColour (int orderIndex);

juce::String formatDb (float db);

/** Text colour on a filled danger (red) background, readable in both themes. */
inline juce::Colour onDanger() { return isDark() ? pal().bg : juce::Colours::white; }

/** Draws a path icon (24x24 design units, stroked) into an area. */
enum class Icon { routing, interfaces, settings, companion, sun, moon, close, plus, headphones, device, mic, speaker };
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour, float strokeWidth = 2.0f);
void drawLogo (juce::Graphics&, juce::Rectangle<float> area);

/** Look and feel driven by the current palette. Call applyTheme() after setDark(). */
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void applyTheme();

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
};
} // namespace ui

#include "Theme.h"

namespace ui
{
namespace
{
using C = juce::Colour;

Palette makeLight()
{
    Palette p;
    p.bg = C (0xfff7f7f9); p.panel = C (0xffffffff); p.panel2 = C (0xfffbfbfc);
    p.border = C (0xffd9d9e0); p.border2 = C (0xffececf1);
    p.text = C (0xff1c1c22); p.dim = C (0xff5d5d6b); p.faint = C (0xff8e8e9c);
    p.nav = C (0xffe8e8ed); p.navSel = C (0xffd6d6e0); p.track = C (0xffe6e6ec);
    p.accent = C (0xff2f5fd0); p.wireIdle = C (0xffc4c4cf); p.grid = C (0xffdcdce4);
    p.green = C (0xff1f9d55); p.okBg = C (0xffe3f4ea); p.okFg = C (0xff146c36);
    p.danger = C (0xffb42318); p.dangerBorder = C (0xffe5b9b9); p.warn = C (0xffc77700);
    p.stripSide = C (0xff5b7fd6);
    p.sources = { C (0xff3b6fe0), C (0xffd9822b), C (0xff8b5cf6), C (0xff0f9d8a),
                  C (0xffc2410c), C (0xff0e7490), C (0xffdb2777), C (0xff475569) };
    return p;
}

Palette makeDark()
{
    Palette p;
    p.bg = C (0xff11141a); p.panel = C (0xff1c212b); p.panel2 = C (0xff181c24);
    p.border = C (0xff2b3240); p.border2 = C (0xff262c37);
    p.text = C (0xffe8ebf1); p.dim = C (0xff9aa3b5); p.faint = C (0xff737c8f);
    p.nav = C (0xff171b22); p.navSel = C (0xff252b36); p.track = C (0xff2b3240);
    p.accent = C (0xff6b96ff); p.wireIdle = C (0xff3a4150); p.grid = C (0xff232833);
    p.green = C (0xff3ecf7a); p.okBg = C (0xff16301f); p.okFg = C (0xff6fe0a0);
    p.danger = C (0xffff8a8a); p.dangerBorder = C (0xff5a2f33); p.warn = C (0xfff0b44c);
    p.stripSide = C (0xff4c6db8);
    p.sources = { C (0xff5b8cff), C (0xffff9f43), C (0xffa78bfa), C (0xff2dd4bf),
                  C (0xfffb7c4b), C (0xff38bdf8), C (0xfff472b6), C (0xff94a3b8) };
    return p;
}

const Palette lightPalette = makeLight();
const Palette darkPalette = makeDark();
bool darkMode = false;
} // namespace

const Palette& pal()    { return darkMode ? darkPalette : lightPalette; }
bool isDark()           { return darkMode; }
void setDark (bool d)   { darkMode = d; }

juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font monoFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

juce::Colour sourceColour (int orderIndex)
{
    return pal().sources[(size_t) (orderIndex % 8 + 8) % 8];
}

juce::String formatDb (float db)
{
    if (db <= -60.0f)
        return "off";
    return (db > 0.05f ? "+" : "") + juce::String (db, 1).replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92"));
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth)
{
    juce::Path p;
    switch (icon)
    {
        case Icon::routing:
            p.addEllipse (3, 4, 4, 4); p.addEllipse (3, 16, 4, 4); p.addEllipse (17, 10, 4, 4);
            p.startNewSubPath (7, 6); p.cubicTo (13, 6, 13, 12, 17, 12);
            p.startNewSubPath (7, 18); p.cubicTo (13, 18, 13, 12, 17, 12);
            break;
        case Icon::interfaces:
        case Icon::device:
            p.addRoundedRectangle (2.0f, 7.0f, 20.0f, 10.0f, 2.0f);
            p.addEllipse (6, 11, 2, 2); p.addEllipse (11, 11, 2, 2); p.addEllipse (16, 11, 2, 2);
            break;
        case Icon::settings:
            p.addEllipse (9, 9, 6, 6);
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                p.startNewSubPath (12 + 6.5f * std::cos (a), 12 + 6.5f * std::sin (a));
                p.lineTo (12 + 9.5f * std::cos (a), 12 + 9.5f * std::sin (a));
            }
            break;
        case Icon::companion: // a 2x2 button deck
            p.addRoundedRectangle (3.0f, 3.0f, 7.5f, 7.5f, 2.0f);
            p.addRoundedRectangle (13.5f, 3.0f, 7.5f, 7.5f, 2.0f);
            p.addRoundedRectangle (3.0f, 13.5f, 7.5f, 7.5f, 2.0f);
            p.addRoundedRectangle (13.5f, 13.5f, 7.5f, 7.5f, 2.0f);
            break;
        case Icon::sun:
            p.addEllipse (8, 8, 8, 8);
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                p.startNewSubPath (12 + 7.0f * std::cos (a), 12 + 7.0f * std::sin (a));
                p.lineTo (12 + 9.5f * std::cos (a), 12 + 9.5f * std::sin (a));
            }
            break;
        case Icon::moon:
            p.startNewSubPath (15, 4);
            p.cubicTo (9, 5, 7, 13, 11, 17);
            p.cubicTo (14, 20, 19, 19, 21, 15);
            p.cubicTo (13, 17, 10, 9, 15, 4);
            p.closeSubPath();
            break;
        case Icon::close:
            p.startNewSubPath (6, 6); p.lineTo (18, 18);
            p.startNewSubPath (18, 6); p.lineTo (6, 18);
            break;
        case Icon::plus:
            p.startNewSubPath (12, 5); p.lineTo (12, 19);
            p.startNewSubPath (5, 12); p.lineTo (19, 12);
            break;
        case Icon::mic:
            p.addRoundedRectangle (9.0f, 3.0f, 6.0f, 11.0f, 3.0f);
            p.startNewSubPath (5.5f, 11.0f);
            p.cubicTo (5.5f, 19.0f, 18.5f, 19.0f, 18.5f, 11.0f);
            p.startNewSubPath (12.0f, 17.0f); p.lineTo (12.0f, 21.0f);
            p.startNewSubPath (8.5f, 21.0f); p.lineTo (15.5f, 21.0f);
            break;
        case Icon::speaker:
            p.startNewSubPath (4.0f, 9.5f); p.lineTo (8.0f, 9.5f); p.lineTo (12.5f, 5.0f);
            p.lineTo (12.5f, 19.0f); p.lineTo (8.0f, 14.5f); p.lineTo (4.0f, 14.5f); p.closeSubPath();
            p.startNewSubPath (15.5f, 9.0f); p.cubicTo (17.5f, 10.5f, 17.5f, 13.5f, 15.5f, 15.0f);
            p.startNewSubPath (18.0f, 6.5f); p.cubicTo (21.5f, 9.5f, 21.5f, 14.5f, 18.0f, 17.5f);
            break;
        case Icon::headphones:
            p.startNewSubPath (4, 17); p.lineTo (4, 12);
            p.cubicTo (4, 2, 20, 2, 20, 12); p.lineTo (20, 17);
            p.addRoundedRectangle (3.0f, 14.0f, 4.0f, 6.0f, 1.5f);
            p.addRoundedRectangle (17.0f, 14.0f, 4.0f, 6.0f, 1.5f);
            break;
    }

    const auto box = juce::Rectangle<float> (0, 0, 24, 24);
    p.applyTransform (juce::RectanglePlacement (juce::RectanglePlacement::centred).getTransformToFit (box, area));
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (strokeWidth * area.getWidth() / 24.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float s = area.getWidth() / 26.0f;
    auto at = [&] (float x, float y) { return juce::Point<float> (area.getX() + x * s, area.getY() + y * s); };

    g.setColour (juce::Colour (0xff2b3aa8));
    g.fillRoundedRectangle (area, 6.0f * s);

    juce::Path arch;
    arch.startNewSubPath (at (5, 18));
    arch.cubicTo (at (6, 6), at (20, 6), at (21, 18));
    arch.startNewSubPath (at (4, 18));
    arch.lineTo (at (22, 18));
    g.setColour (juce::Colours::white);
    g.strokePath (arch, juce::PathStrokeType (2.2f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path bars;
    bars.startNewSubPath (at (10, 17)); bars.lineTo (at (10, 13));
    bars.startNewSubPath (at (13, 17)); bars.lineTo (at (13, 10));
    bars.startNewSubPath (at (16, 17)); bars.lineTo (at (16, 12.5f));
    g.setColour (juce::Colour (0xff4ade80));
    g.strokePath (bars, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    applyTheme();
}

void LookAndFeel::applyTheme()
{
    const auto& p = pal();
    setColourScheme ({ p.bg, p.panel, p.panel, p.border, p.text, p.accent, juce::Colours::white, p.accent, p.text });

    setColour (juce::ResizableWindow::backgroundColourId, p.bg);
    setColour (juce::DocumentWindow::backgroundColourId, p.bg);
    setColour (juce::Label::textColourId, p.text);
    setColour (juce::TextEditor::backgroundColourId, p.panel);
    setColour (juce::TextEditor::textColourId, p.text);
    setColour (juce::TextEditor::outlineColourId, p.border);
    setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
    setColour (juce::TextEditor::highlightColourId, p.accent.withAlpha (0.25f));
    setColour (juce::CaretComponent::caretColourId, p.text);
    setColour (juce::ComboBox::backgroundColourId, p.panel);
    setColour (juce::ComboBox::textColourId, p.text);
    setColour (juce::ComboBox::outlineColourId, p.border);
    setColour (juce::ComboBox::arrowColourId, p.dim);
    setColour (juce::PopupMenu::backgroundColourId, p.panel);
    setColour (juce::PopupMenu::textColourId, p.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxTextColourId, p.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::backgroundColourId, p.track);
    setColour (juce::Slider::trackColourId, p.accent);
    setColour (juce::ScrollBar::thumbColourId, p.faint);
    setColour (juce::TooltipWindow::backgroundColourId, p.panel);
    setColour (juce::TooltipWindow::textColourId, p.text);
    setColour (juce::TooltipWindow::outlineColourId, p.border);
    setColour (juce::HyperlinkButton::textColourId, p.accent);
    setColour (juce::AlertWindow::backgroundColourId, p.panel);
    setColour (juce::AlertWindow::textColourId, p.text);
    setColour (juce::ListBox::backgroundColourId, p.panel);
}

juce::Slider::SliderLayout LookAndFeel::getSliderLayout (juce::Slider& s)
{
    auto layout = juce::LookAndFeel_V4::getSliderLayout (s);
    if (s.isHorizontal())
        layout.sliderBounds = layout.sliderBounds.reduced (8, 0); // room for the round thumb
    else if (s.isVertical())
        layout.sliderBounds = layout.sliderBounds.reduced (0, 8);
    return layout;
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (monoFont (12.5f));
    l->setJustificationType (juce::Justification::centredRight);
    return l;
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                    float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto& p = pal();
    const auto fill = slider.findColour (juce::Slider::trackColourId);
    const bool enabled = slider.isEnabled();

    if (style == juce::Slider::LinearHorizontal)
    {
        const float cy = (float) y + (float) height * 0.5f;
        const auto track = juce::Rectangle<float> ((float) x, cy - 3.0f, (float) width, 6.0f);
        g.setColour (p.track);
        g.fillRoundedRectangle (track, 3.0f);
        g.setColour (enabled ? fill : p.faint);
        g.fillRoundedRectangle (track.withRight (sliderPos), 3.0f);

        const auto thumb = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ sliderPos, cy });
        g.setColour (p.panel);
        g.fillEllipse (thumb);
        g.setColour (enabled ? fill : p.faint);
        g.drawEllipse (thumb.reduced (1.0f), 2.0f);
    }
    else
    {
        // Fader: thin track and a flat cap.
        const float cx = (float) x + (float) width * 0.5f;
        g.setColour (p.track);
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - 2.0f, (float) y, 4.0f, (float) height), 2.0f);

        const auto cap = juce::Rectangle<float> (juce::jmin (30.0f, (float) width), 14.0f).withCentre ({ cx, sliderPos });
        g.setColour (p.panel);
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (enabled ? fill : p.faint);
        g.drawRoundedRectangle (cap.reduced (1.0f), 4.0f, 2.0f);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& p = pal();
    const auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (box.hasKeyboardFocus (true) ? p.accent : p.border);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 18.0f, ay = (float) height * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.0f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (p.dim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)   { return font (13.5f); }
juce::Font LookAndFeel::getPopupMenuFont()                  { return font (14.0f); }

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, (float) width, (float) height), 7.0f);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;
    g.setColour (editor.hasKeyboardFocus (true) ? pal().accent : editor.findColour (juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f), 7.0f, 1.0f);
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                 int thumbStart, int thumbSize, bool isMouseOver, bool)
{
    auto thumb = vertical ? juce::Rectangle<int> (x, y + thumbStart, width, thumbSize)
                          : juce::Rectangle<int> (x + thumbStart, y, thumbSize, height);
    g.setColour (pal().faint.withAlpha (isMouseOver ? 0.8f : 0.5f));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), 3.0f);
}
} // namespace ui

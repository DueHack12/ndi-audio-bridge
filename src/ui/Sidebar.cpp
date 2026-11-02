#include "Sidebar.h"

namespace ui
{
namespace
{
struct NavItem { const char* label; Icon icon; Sidebar::Page page; };
const NavItem navItems[] = {
    { "Routing", Icon::routing, Sidebar::Page::routing },
    { "Interfaces", Icon::interfaces, Sidebar::Page::interfaces },
    { "Settings", Icon::settings, Sidebar::Page::settings },
};

juce::String bitsText (double bps)
{
    if (bps < 0) return "?";
    if (bps >= 1.0e9) return juce::String (bps / 1.0e9, 1) + " Gb/s";
    if (bps >= 1.0e6) return juce::String (bps / 1.0e6, 1) + " Mb/s";
    return juce::String (juce::roundToInt (bps / 1.0e3)) + " kb/s";
}
} // namespace

void Sidebar::setExpanded (bool e)
{
    if (expanded == e)
        return;
    expanded = e;
    if (onExpandedChanged)
        onExpandedChanged();
    repaint();
}

void Sidebar::tick()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto mouse = getMouseXYRelative();
    const bool over = isShowing() && juce::Rectangle<int> (0, 0, panelWidth(), getHeight()).contains (mouse)
                      && juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse() != nullptr;

    if (! expanded)
    {
        if (over)
        {
            if (hoverSince == 0)
                hoverSince = now;
            else if (now - hoverSince > 350)
                setExpanded (true);
        }
        else
        {
            hoverSince = 0;
        }
    }
    else
    {
        if (over)
            leftSince = 0;
        else if (leftSince == 0)
            leftSince = now;
        else if (now - leftSince > 150)
        {
            leftSince = 0;
            hoverSince = 0;
            setExpanded (false);
        }
    }
}

juce::Rectangle<int> Sidebar::navRect (int index) const
{
    return { 10, 76 + index * 48, panelWidth() - 20, 42 };
}

juce::Rectangle<int> Sidebar::themeRect() const
{
    return { 10, getHeight() - 54, panelWidth() - 20, 40 };
}

void Sidebar::paint (juce::Graphics& g)
{
    const auto& p = pal();
    const int w = panelWidth();
    const auto panel = juce::Rectangle<int> (0, 0, w, getHeight());

    if (expanded)
    {
        juce::ColourGradient shadow (juce::Colours::black.withAlpha (isDark() ? 0.35f : 0.12f), (float) w, 0.0f,
                                     juce::Colours::transparentBlack, (float) (w + shadowWidth), 0.0f, false);
        g.setGradientFill (shadow);
        g.fillRect (w, 0, shadowWidth, getHeight());
    }

    g.setColour (p.nav);
    g.fillRect (panel);
    g.setColour (p.border);
    g.drawVerticalLine (w - 1, 0.0f, (float) getHeight());

    drawLogo (g, juce::Rectangle<float> (16.0f, 18.0f, 32.0f, 32.0f));
    if (expanded)
    {
        g.setColour (p.text);
        g.setFont (font (15.0f, true));
        g.drawText ("NDI Audio Bridge", juce::Rectangle<int> (58, 18, w - 64, 32), juce::Justification::centredLeft, false);
    }

    for (int i = 0; i < 3; ++i)
    {
        const auto r = navRect (i);
        const bool sel = navItems[i].page == page;
        if (sel)
        {
            g.setColour (p.navSel);
            g.fillRoundedRectangle (r.toFloat(), 10.0f);
        }
        drawIcon (g, navItems[i].icon, juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ (float) r.getX() + 22.0f, (float) r.getCentreY() }),
                  sel ? p.text : p.dim);
        if (expanded)
        {
            g.setColour (sel ? p.text : p.dim);
            g.setFont (font (14.0f, sel));
            g.drawText (navItems[i].label, r.withTrimmedLeft (46), juce::Justification::centredLeft, false);
        }
    }

    // ---- resources
    const float cpuUsed = res.systemCpu >= 0 ? juce::jlimit (0.0f, 100.0f, res.systemCpu) : 0.0f;
    const float appCpu = res.appCpu >= 0 ? juce::jlimit (0.0f, cpuUsed, res.appCpu) : 0.0f;
    const double netBusiest = juce::jmax (res.netRxBitsPerSec, res.netTxBitsPerSec);
    const bool haveLink = res.linkBitsPerSec > 0 && netBusiest >= 0;
    const float netUsed = haveLink ? (float) juce::jlimit (0.0, 100.0, 100.0 * netBusiest / res.linkBitsPerSec) : 0.0f;
    const float ndiShare = haveLink ? (float) juce::jlimit (0.0, (double) netUsed, 100.0 * juce::jmax (res.ndiTxBitsPerSec, res.ndiRxBitsPerSec) / res.linkBitsPerSec) : 0.0f;

    auto bar = [&] (juce::Rectangle<float> r, float total, float part)
    {
        g.setColour (p.track);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (p.faint);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jmax (total, 2.0f) / 100.0f), r.getHeight() * 0.5f);
        g.setColour (p.accent);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * part / 100.0f), r.getHeight() * 0.5f);
    };

    const int themeTop = themeRect().getY();

    if (expanded)
    {
        int y = themeTop - 152;
        g.setColour (p.border);
        g.drawHorizontalLine (y - 10, 10.0f, (float) w - 10.0f);
        g.setColour (p.dim);
        g.setFont (font (11.0f, true));
        g.drawText ("RESOURCES", 18, y, w - 36, 14, juce::Justification::centredLeft, false);
        y += 22;

        auto block = [&] (const juce::String& title, const juce::String& free, float total, float part, const juce::String& detail)
        {
            g.setColour (p.text);
            g.setFont (font (13.0f, true));
            g.drawText (title, 18, y, 100, 16, juce::Justification::centredLeft, false);
            g.setColour (p.dim);
            g.setFont (font (12.0f));
            g.drawText (free, 18, y, w - 36, 16, juce::Justification::centredRight, false);
            y += 20;
            bar (juce::Rectangle<float> (18.0f, (float) y, (float) w - 36.0f, 6.0f), total, part);
            y += 10;
            g.setColour (p.dim);
            g.setFont (font (11.5f));
            g.drawText (detail, 18, y, w - 36, 15, juce::Justification::centredLeft, true);
            y += 26;
        };

        block ("CPU", res.systemCpu >= 0 ? juce::String (juce::roundToInt (100.0f - cpuUsed)) + "% free" : juce::String ("n/a"),
               cpuUsed, appCpu,
               "This app " + juce::String (juce::roundToInt (appCpu)) + juce::String::fromUTF8 ("% · everything ") + juce::String (juce::roundToInt (cpuUsed)) + "%");

        block ("Network", haveLink ? juce::String (juce::roundToInt (100.0f - netUsed)) + "% free" : bitsText (netBusiest),
               netUsed, ndiShare,
               juce::String::fromUTF8 ("NDI \xe2\x86\x91 ") + bitsText (res.ndiTxBitsPerSec) + juce::String::fromUTF8 (" \xe2\x86\x93 ")
                   + bitsText (res.ndiRxBitsPerSec) + (haveLink ? juce::String::fromUTF8 (" · ") + bitsText (res.linkBitsPerSec) + " link" : juce::String()));

        g.setColour (p.dim);
        g.setFont (font (11.5f));
        g.drawText ("Audio engine load " + juce::String (juce::roundToInt (res.audioLoad * 100.0f)) + "%", 18, y - 8, w - 36, 15,
                    juce::Justification::centredLeft, false);
    }
    else
    {
        int y = themeTop - 108;
        g.setColour (p.border);
        g.drawHorizontalLine (y - 10, 10.0f, (float) w - 10.0f);

        auto mini = [&] (const juce::String& label, float total, float part, const juce::String& value)
        {
            g.setColour (p.dim);
            g.setFont (font (10.0f, true));
            g.drawText (label, 0, y, w, 12, juce::Justification::centred, false);
            y += 15;
            bar (juce::Rectangle<float> ((float) w * 0.5f - 16.0f, (float) y, 32.0f, 5.0f), total, part);
            y += 9;
            g.setColour (p.text);
            g.drawText (value, 0, y, w, 12, juce::Justification::centred, false);
            y += 22;
        };

        mini ("CPU", cpuUsed, appCpu, res.systemCpu >= 0 ? juce::String (juce::roundToInt (cpuUsed)) + "%" : juce::String ("-"));
        mini ("NET", netUsed, ndiShare, haveLink ? juce::String (juce::roundToInt (netUsed)) + "%" : bitsText (netBusiest).upToFirstOccurrenceOf (" ", false, false));
    }

    // ---- theme switch
    const auto tr = themeRect();
    g.setColour (p.panel);
    g.fillRoundedRectangle (tr.toFloat(), 10.0f);
    g.setColour (p.border);
    g.drawRoundedRectangle (tr.toFloat().reduced (0.5f), 10.0f, 1.0f);
    drawIcon (g, isDark() ? Icon::moon : Icon::sun,
              juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ (float) tr.getX() + 22.0f, (float) tr.getCentreY() }), p.text);
    if (expanded)
    {
        g.setColour (p.text);
        g.setFont (font (13.5f, true));
        g.drawText (isDark() ? "Dark mode" : "Light mode", tr.withTrimmedLeft (44), juce::Justification::centredLeft, false);
    }
}

void Sidebar::mouseUp (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    for (int i = 0; i < 3; ++i)
    {
        if (navRect (i).contains (pos))
        {
            setPage (navItems[i].page);
            if (onNavigate)
                onNavigate (navItems[i].page);
            return;
        }
    }

    if (themeRect().contains (pos) && onToggleTheme)
        onToggleTheme();
}

void Sidebar::mouseMove (const juce::MouseEvent& e)
{
    bool clickable = themeRect().contains (e.getPosition());
    for (int i = 0; i < 3; ++i)
        clickable = clickable || navRect (i).contains (e.getPosition());
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}
} // namespace ui

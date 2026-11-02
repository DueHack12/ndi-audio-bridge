#pragma once

#include "Theme.h"

namespace ui
{
struct ResourceInfo
{
    float appCpu = -1.0f, systemCpu = -1.0f, audioLoad = 0.0f;
    double ndiTxBitsPerSec = 0.0, ndiRxBitsPerSec = 0.0;
    double netRxBitsPerSec = -1.0, netTxBitsPerSec = -1.0, linkBitsPerSec = -1.0;
};

/** Collapsed icon bar that expands (over the page) when the mouse dwells on it.
    Bottom: CPU / network glance and the light/dark switch. */
class Sidebar : public juce::Component
{
public:
    enum class Page { routing, interfaces, companion, settings };

    static constexpr int collapsedWidth = 64, expandedWidth = 236, shadowWidth = 18;

    Sidebar() { setOpaque (false); }

    std::function<void (Page)> onNavigate;
    std::function<void()> onToggleTheme;
    std::function<void()> onExpandedChanged;

    void setPage (Page p)                       { page = p; repaint(); }
    void setResources (const ResourceInfo& r)   { res = r; repaint(); }
    bool isExpanded() const noexcept            { return expanded; }

    /** Call ~30 times a second: handles the hover dwell. */
    void tick();

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> navRect (int index) const;
    juce::Rectangle<int> themeRect() const;
    int panelWidth() const { return expanded ? expandedWidth : collapsedWidth; }
    void setExpanded (bool);

    Page page = Page::routing;
    bool expanded = false;
    juce::uint32 hoverSince = 0, leftSince = 0;
    ResourceInfo res;
};
} // namespace ui

#include "RoutingPage.h"
#include "ndi/NdiLibrary.h"

namespace ui
{
RoutingPage::RoutingPage (AudioEngine& e) : engine (e), patch (e), mixer (e), inspector (e)
{
    addAndMakeVisible (patch);
    addAndMakeVisible (mixer);
    addChildComponent (inspector);

    addInterfaceButton.setIcon (Icon::plus);
    addInterfaceButton.setFontHeight (12.5f);
    addInterfaceButton.onClick = [this] { if (onAddInterface) onAddInterface(); };
    addAndMakeVisible (addInterfaceButton);

    patch.onSourceClicked = [this] (int s)
    {
        if (inspector.isVisible() && inspector.getSource() == s)
            closeInspector();
        else
            openInspector (s);
    };

    patch.onMixClicked = [this] (int m)
    {
        selectedMix = m;
        mixer.setSelectedMix (m);
        patch.setSelection (selectedSource, selectedMix);
    };

    patch.onRouteDropped = [this] (int s, int m)
    {
        if (! juce::isPositiveAndBelow (m, (int) mixes.size()))
            return;
        if (! RoutingModel::isRouted (engine, mixes[(size_t) m], s))
            RoutingModel::setRouted (engine, mixes[(size_t) m], s, true);
        selectedMix = m;
        mixer.setSelectedMix (m);
        patch.setSelection (selectedSource, selectedMix);
        routingChanged();
    };

    mixer.onMixSelected = [this] (int m)
    {
        selectedMix = m;
        patch.setSelection (selectedSource, selectedMix);
    };
    mixer.onRoutingChanged = [this] { routingChanged(); refreshNames(); };
    mixer.onStructureChanged = [this]
    {
        rebuild();
        if (onChanged) onChanged();
    };

    inspector.onClose = [this] { closeInspector(); };
    inspector.onRoutingChanged = [this]
    {
        patch.repaint();
        mixer.refreshValues();
        if (onChanged) onChanged();
    };
    inspector.onSourceChanged = [this] { refreshNames(); if (onChanged) onChanged(); };
}

void RoutingPage::rebuild()
{
    sources = RoutingModel::listSources (engine);
    mixes = RoutingModel::listMixes (engine);
    selectedMix = juce::jlimit (0, juce::jmax (0, (int) mixes.size() - 1), selectedMix);

    patch.rebuild (sources, mixes);
    mixer.rebuild (sources, mixes, [this] (int s) { return patch.colourFor (s); });
    mixer.setSelectedMix (selectedMix);

    if (selectedSource >= 0 && std::find (sources.begin(), sources.end(), selectedSource) == sources.end())
        closeInspector();
    else if (inspector.isVisible())
        inspector.show (selectedSource, patch.colourFor (selectedSource), mixes);

    patch.setSelection (selectedSource, selectedMix);
    resized();
    repaint();
}

void RoutingPage::refreshNames()
{
    patch.rebuild (sources, mixes);
    patch.setSelection (selectedSource, selectedMix);
    mixer.refreshValues();
}

void RoutingPage::routingChanged()
{
    patch.repaint();
    inspector.refreshRoutes();
    mixer.refreshValues();
    if (onChanged)
        onChanged();
}

void RoutingPage::openInspector (int s)
{
    selectedSource = s;
    inspector.show (s, patch.colourFor (s), mixes);
    inspector.setVisible (true);
    patch.setSelection (selectedSource, selectedMix);
    resized();
}

void RoutingPage::closeInspector()
{
    selectedSource = -1;
    inspector.setVisible (false); // the panel is rebuilt on the next open, outside its own callbacks
    patch.setSelection (selectedSource, selectedMix);
    resized();
}

void RoutingPage::tick (const Levels& levels)
{
    patch.updateLevels (levels);
    mixer.updateLevels (levels);
    if (inspector.isVisible())
        inspector.tick (levels);
    repaint (headerArea);
}

void RoutingPage::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.fillAll (p.bg);

    auto h = headerArea.reduced (24, 0);
    g.setColour (p.text);
    g.setFont (font (20.0f, true));
    g.drawText ("Routing", h.removeFromLeft (90), juce::Justification::centredLeft, false);
    g.setColour (p.border2);
    g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, (float) getWidth());

    auto chip = [&] (const juce::String& text, juce::Colour bg, juce::Colour fg, juce::Colour dot, bool outline)
    {
        g.setFont (font (12.5f, true));
        const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + (dot.isTransparent() ? 22 : 36);
        auto r = h.removeFromLeft (w).withSizeKeepingCentre (w, 26).toFloat();
        h.removeFromLeft (8);
        g.setColour (bg);
        g.fillRoundedRectangle (r, 13.0f);
        if (outline)
        {
            g.setColour (p.border);
            g.drawRoundedRectangle (r.reduced (0.5f), 13.0f, 1.0f);
        }
        if (! dot.isTransparent())
        {
            g.setColour (dot);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 15.0f, r.getCentreY() }));
            r.removeFromLeft (14.0f);
        }
        g.setColour (fg);
        g.drawText (text, r, juce::Justification::centred, false);
    };

    h.removeFromLeft (8);
    if (ndi::lib().isLoaded())
    {
        int live = 0;
        for (auto s : sources)
            if (s != bridge::returnSource && engine.getSendHub().getSlot (s).live.load())
                ++live;
        chip ("NDI " + ndi::lib().getStatusText().fromLastOccurrenceOf (" ", false, false) + juce::String::fromUTF8 (" · ") + juce::String (live) + " live",
              p.okBg, p.okFg, juce::Colours::transparentBlack, false);
    }
    else
    {
        chip ("NDI runtime not installed", p.danger.withAlpha (0.12f), p.danger, juce::Colours::transparentBlack, false);
    }

    for (int i = 0; i < bridge::maxInterfaces; ++i)
        if (engine.isInterfaceUsed (i))
            chip (RoutingModel::shortDevice (engine, i), p.panel, p.text, engine.isInterfaceRunning (i) ? p.green : p.danger, true);

    if (! inspector.isVisible() && h.getWidth() > 300)
    {
        g.setColour (p.dim);
        g.setFont (font (12.5f));
        g.drawText (juce::String::fromUTF8 ("Drag from a source\xe2\x80\x99s dot onto a mix to add it"),
                    headerArea.reduced (24, 0), juce::Justification::centredRight, false);
    }
}

void RoutingPage::resized()
{
    auto r = getLocalBounds();
    if (inspector.isVisible())
        inspector.setBounds (r.removeFromRight (SourceInspector::panelWidth));

    headerArea = r.removeFromTop (56);

    // Place the add button after the chips (approximate their width).
    int x = headerArea.getX() + 24 + 90 + 8;
    auto chipW = [] (const juce::String& t, bool dot) { return (int) juce::GlyphArrangement::getStringWidth (font (12.5f, true), t) + (dot ? 36 : 22) + 8; };
    x += chipW (ndi::lib().isLoaded() ? juce::String::fromUTF8 ("NDI 6.3 · 00 live") : "NDI runtime not installed", false);
    for (int i = 0; i < bridge::maxInterfaces; ++i)
        if (engine.isInterfaceUsed (i))
            x += chipW (RoutingModel::shortDevice (engine, i), true);
    addInterfaceButton.setBounds (x, headerArea.getCentreY() - 13, 132, 26);
    addInterfaceButton.setVisible ([this] { for (int i = 0; i < bridge::maxInterfaces; ++i) if (! engine.isInterfaceUsed (i)) return true; return false; }());

    const int patchH = juce::jmax (240, juce::roundToInt ((float) r.getHeight() * 0.48f));
    patch.setBounds (r.removeFromTop (patchH));
    mixer.setBounds (r);
}
} // namespace ui

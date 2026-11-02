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

    // Chip text ("2 live", interface up/down) can change: re-lay out the header when it does.
    const auto previous = chips;
    layoutHeader();
    bool same = previous.size() == chips.size();
    for (size_t i = 0; same && i < chips.size(); ++i)
        same = previous[i].text == chips[i].text && previous[i].kind == chips[i].kind;
    if (! same)
        repaint (headerArea);
}

void RoutingPage::layoutHeader()
{
    chips.clear();

    auto add = [this] (const juce::String& text, int kind)
    {
        const bool dot = kind >= 2;
        const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (12.5f, true), text)) + (dot ? 40 : 26);
        chips.push_back ({ text, { 0, 0, w, 26 }, kind });
    };

    if (ndi::lib().isLoaded())
    {
        int live = 0;
        for (auto s : sources)
            if (s != bridge::returnSource && engine.getSendHub().getSlot (s).live.load())
                ++live;
        add ("NDI " + ndi::lib().getStatusText().fromLastOccurrenceOf (" ", false, false)
               + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (live) + " live", 0);
    }
    else
    {
        add ("NDI runtime not installed", 1);
    }

    for (int i = 0; i < bridge::maxInterfaces; ++i)
        if (engine.isInterfaceUsed (i))
            add (RoutingModel::shortDevice (engine, i), engine.isInterfaceRunning (i) ? 2 : 3);

    int x = headerArea.getX() + 24 + 90 + 8;
    for (auto& c : chips)
    {
        c.bounds.setPosition (x, headerArea.getCentreY() - 13);
        x += c.bounds.getWidth() + 8;
    }

    bool canAdd = false;
    for (int i = 0; i < bridge::maxInterfaces; ++i)
        canAdd = canAdd || ! engine.isInterfaceUsed (i);

    addInterfaceButton.setVisible (canAdd);
    addInterfaceButton.setBounds (x, headerArea.getCentreY() - 13, 132, 26);
    hintLeft = x + (canAdd ? 132 + 16 : 0);
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

    g.setFont (font (12.5f, true));
    for (auto& c : chips)
    {
        auto r = c.bounds.toFloat();
        const juce::Colour bg = c.kind == 0 ? p.okBg : (c.kind == 1 ? p.danger.withAlpha (0.12f) : p.panel);
        const juce::Colour fg = c.kind == 0 ? p.okFg : (c.kind == 1 ? p.danger : p.text);

        g.setColour (bg);
        g.fillRoundedRectangle (r, 13.0f);
        if (c.kind >= 2)
        {
            g.setColour (p.border);
            g.drawRoundedRectangle (r.reduced (0.5f), 13.0f, 1.0f);
            g.setColour (c.kind == 2 ? p.green : p.danger);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 15.0f, r.getCentreY() }));
            r.removeFromLeft (14.0f);
        }
        g.setColour (fg);
        g.drawText (c.text, r, juce::Justification::centred, false);
    }

    const auto hint = juce::String::fromUTF8 ("Drag from a source\xe2\x80\x99s dot onto a mix to add it");
    g.setFont (font (12.5f));
    if (! inspector.isVisible()
        && headerArea.getRight() - 24 - hintLeft > (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), hint) + 8)
    {
        g.setColour (p.dim);
        g.drawText (hint, headerArea.reduced (24, 0), juce::Justification::centredRight, false);
    }
}

void RoutingPage::resized()
{
    auto r = getLocalBounds();
    if (inspector.isVisible())
        inspector.setBounds (r.removeFromRight (SourceInspector::panelWidth));

    headerArea = r.removeFromTop (56);
    layoutHeader();

    const int patchH = juce::jmax (240, juce::roundToInt ((float) r.getHeight() * 0.48f));
    patch.setBounds (r.removeFromTop (patchH));
    mixer.setBounds (r);
}
} // namespace ui

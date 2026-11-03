#include "PatchView.h"

namespace ui
{
void PatchView::rebuild (const std::vector<int>& sources, const std::vector<MixRef>& mixRefs)
{
    srcs.clear();
    mixes.clear();

    int order = 0;
    for (auto s : sources)
    {
        Src item;
        item.source = s;
        item.order = order++;
        item.name = engine.getSourceConfig (s).name;
        item.tag = RoutingModel::sourceTag (s);
        item.cough = engine.getSourceConfig (s).sendMuted;
        srcs.push_back (item);
    }

    for (auto& m : mixRefs)
    {
        Mix item;
        item.ref = m;
        item.name = RoutingModel::name (engine, m);
        item.sub = RoutingModel::subtitle (engine, m);
        mixes.push_back (item);
    }

    layoutItems();
    repaint();
}

juce::Colour PatchView::colourFor (int source) const
{
    for (auto& s : srcs)
        if (s.source == source)
            return sourceColour (s.order);
    return pal().faint;
}

void PatchView::setSelection (int source, int mixIndex)
{
    selectedSource = source;
    selectedMix = mixIndex;
    repaint();
}

void PatchView::updateLevels (const Levels& levels)
{
    for (auto& s : srcs)
    {
        // cough can also change from Companion
        if (const bool cough = engine.getSourceConfig (s.source).sendMuted; cough != s.cough)
        {
            s.cough = cough;
            repaint (s.box.expanded (6.0f).toNearestInt());
        }

        const float db = juce::Decibels::gainToDecibels (levels.source[(size_t) s.source], -60.0f);
        const float next = db > s.level ? db : juce::jmax (db, s.level - 1.8f);
        if (std::abs (next - s.level) > 0.05f)
        {
            s.level = next;
            repaint (s.box.withTrimmedLeft (s.box.getWidth() - 50.0f).toNearestInt());
        }
    }
}

void PatchView::layoutItems()
{
    srcLabels.clear();
    mixLabels.clear();

    const float w = (float) getWidth(), h = (float) getHeight();
    const float labelH = 20.0f, groupGap = 10.0f, top = 6.0f;

    // ---- sources, grouped by interface (computer & NDI return last)
    auto groupOfSource = [] (int s) { return s >= bridge::computerSource ? bridge::maxInterfaces : bridge::interfaceOfSource (s); };

    int srcGroups = 0;
    for (size_t i = 0; i < srcs.size(); ++i)
        if (i == 0 || groupOfSource (srcs[i].source) != groupOfSource (srcs[i - 1].source))
            ++srcGroups;

    const float srcSpacing = srcs.empty() ? 36.0f
        : juce::jlimit (24.0f, 38.0f, (h - top - 6.0f - (float) srcGroups * labelH - (float) juce::jmax (0, srcGroups - 1) * groupGap) / (float) srcs.size());
    const float srcW = juce::jmin (250.0f, w * 0.26f);
    float y = top;

    for (size_t i = 0; i < srcs.size(); ++i)
    {
        const int grp = groupOfSource (srcs[i].source);
        if (i == 0 || grp != groupOfSource (srcs[i - 1].source))
        {
            if (i != 0) y += groupGap;
            srcLabels.push_back ({ grp == bridge::maxInterfaces ? juce::String ("COMPUTER & NETWORK")
                                                                : engine.getInputDeviceName (grp).toUpperCase(),
                                   { 24.0f, y } });
            y += labelH;
        }

        srcs[i].box = { 24.0f, y, srcW, srcSpacing - 4.0f };
        y += srcSpacing;
    }

    // ---- mixes, grouped by interface
    int mixGroups = 0;
    for (size_t i = 0; i < mixes.size(); ++i)
        if (i == 0 || bridge::interfaceOfBus (mixes[i].ref.bus) != bridge::interfaceOfBus (mixes[i - 1].ref.bus))
            ++mixGroups;

    const float mixSpacing = mixes.empty() ? 62.0f
        : juce::jlimit (36.0f, 62.0f, (h - top - 6.0f - (float) mixGroups * labelH - (float) juce::jmax (0, mixGroups - 1) * groupGap) / (float) mixes.size());
    const float mixW = juce::jmin (300.0f, w * 0.3f);
    const float mixX = w - 24.0f - mixW;
    y = top;

    for (size_t i = 0; i < mixes.size(); ++i)
    {
        const int grp = bridge::interfaceOfBus (mixes[i].ref.bus);
        if (i == 0 || grp != bridge::interfaceOfBus (mixes[i - 1].ref.bus))
        {
            if (i != 0) y += groupGap;
            mixLabels.push_back ({ engine.getOutputDeviceName (grp).toUpperCase(), { mixX, y } });
            y += labelH;
        }

        mixes[i].box = { mixX, y, mixW, mixSpacing - 8.0f };
        y += mixSpacing;
    }
}

int PatchView::hitSource (juce::Point<float> p) const
{
    for (size_t i = 0; i < srcs.size(); ++i)
        if (srcs[i].box.expanded (8.0f, 0.0f).contains (p))
            return (int) i;
    return -1;
}

int PatchView::hitMix (juce::Point<float> p) const
{
    for (size_t i = 0; i < mixes.size(); ++i)
        if (mixes[i].box.expanded (10.0f, 2.0f).contains (p))
            return (int) i;
    return -1;
}

void PatchView::drawWire (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour c, float width, bool dashed) const
{
    const float pull = juce::jmax (60.0f, (b.x - a.x) * 0.45f);
    juce::Path path;
    path.startNewSubPath (a);
    path.cubicTo (a.translated (pull, 0), b.translated (-pull, 0), b);

    g.setColour (c);
    if (dashed)
    {
        juce::Path dashedPath;
        const float dashes[] = { 6.0f, 6.0f };
        juce::PathStrokeType (width).createDashedStroke (dashedPath, path, dashes, 2);
        g.fillPath (dashedPath);
    }
    else
    {
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void PatchView::paint (juce::Graphics& g)
{
    const auto& p = pal();

    // Dot grid (cached)
    if (gridCache.isNull() || gridCache.getWidth() != getWidth() || gridCache.getHeight() != getHeight())
    {
        gridCache = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), false);
        juce::Graphics gg (gridCache);
        gg.fillAll (p.bg);
        gg.setColour (p.grid);
        for (int gy = 11; gy < getHeight(); gy += 22)
            for (int gx = 11; gx < getWidth(); gx += 22)
                gg.fillRect (gx, gy, 2, 2);
    }
    g.drawImageAt (gridCache, 0, 0);

    if (srcs.size() <= 2 && mixes.empty())
    {
        g.setColour (p.dim);
        g.setFont (font (15.0f));
        g.drawText ("Add an input or output device to start routing", getLocalBounds(), juce::Justification::centred);
    }

    // ---- wires: idle first, highlighted on top
    for (int pass = 0; pass < 2; ++pass)
    {
        for (size_t mi = 0; mi < mixes.size(); ++mi)
        {
            for (auto& s : srcs)
            {
                if (! RoutingModel::isRouted (engine, mixes[mi].ref, s.source))
                    continue;

                const bool hot = selectedSource >= 0 ? s.source == selectedSource : (int) mi == selectedMix;
                if (hot != (pass == 1))
                    continue;

                const float lin = juce::Decibels::decibelsToGain (RoutingModel::level (engine, mixes[mi].ref, s.source));
                drawWire (g, portOf (s), portOf (mixes[mi]).translated (-6.0f, 0),
                          hot ? sourceColour (s.order) : p.wireIdle, hot ? 1.6f + 2.6f * juce::jmin (1.0f, lin) : 1.3f, false);
            }
        }
    }

    if (dragging && juce::isPositiveAndBelow (pressedSource, (int) srcs.size()))
    {
        const auto end = hoverMix >= 0 ? portOf (mixes[(size_t) hoverMix]).translated (-8.0f, 0) : dragPos;
        drawWire (g, portOf (srcs[(size_t) pressedSource]), end, p.accent, 2.2f, true);
    }

    // ---- group labels
    g.setFont (font (11.0f, true));
    g.setColour (p.dim);
    for (auto& l : srcLabels)
        g.drawText (l.text, juce::Rectangle<float> (l.pos.x, l.pos.y, 260.0f, 16.0f), juce::Justification::centredLeft, true);
    for (auto& l : mixLabels)
        g.drawText (l.text, juce::Rectangle<float> (l.pos.x, l.pos.y, 300.0f, 16.0f), juce::Justification::centredLeft, true);

    // ---- source pills
    for (auto& s : srcs)
    {
        const auto colour = sourceColour (s.order);
        const bool sel = s.source == selectedSource;
        auto r = s.box;

        if (sel)
        {
            g.setColour (colour.withAlpha (0.15f));
            g.fillRoundedRectangle (r.expanded (4.0f), 12.0f);
        }

        g.setColour (p.panel);
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (sel ? colour : p.border);
        g.drawRoundedRectangle (r.reduced (sel ? 1.0f : 0.5f), 9.0f, sel ? 2.0f : 1.0f);

        auto inner = r.reduced (12.0f, 0);
        g.setColour (colour);
        g.fillEllipse (inner.removeFromLeft (9.0f).withSizeKeepingCentre (9.0f, 9.0f));
        inner.removeFromLeft (8.0f);

        auto meter = inner.removeFromRight (28.0f).withSizeKeepingCentre (28.0f, 4.0f);
        g.setColour (p.track);
        g.fillRoundedRectangle (meter, 2.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (s.level + 60.0f) / 60.0f);
        g.setColour (s.level > -3.0f ? p.danger : (s.level > -12.0f ? p.warn : p.green));
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * frac), 2.0f);
        inner.removeFromRight (8.0f);

        if (s.cough)
        {
            const auto badge = inner.removeFromRight (52.0f).withSizeKeepingCentre (52.0f, 18.0f);
            g.setColour (p.danger);
            g.fillRoundedRectangle (badge, 9.0f);
            g.setColour (onDanger());
            g.setFont (font (10.0f, true));
            g.drawText ("COUGH", badge, juce::Justification::centred, false);
            inner.removeFromRight (6.0f);
        }

        if (! s.cough || inner.getWidth() > 110.0f)
        {
            g.setFont (font (11.0f));
            g.setColour (p.dim);
            g.drawText (s.tag, inner.removeFromRight (30.0f), juce::Justification::centredRight, false);
        }

        g.setFont (font (13.0f, true));
        g.setColour (p.text);
        g.drawText (s.name, inner, juce::Justification::centredLeft, true);

        // port
        const auto port = juce::Rectangle<float> (14.0f, 14.0f).withCentre (portOf (s));
        g.setColour (colour);
        g.fillEllipse (port);
        g.setColour (p.panel);
        g.drawEllipse (port.reduced (1.5f), 3.0f);
    }

    // ---- mix cards
    for (size_t i = 0; i < mixes.size(); ++i)
    {
        auto& m = mixes[i];
        auto r = m.box;
        const bool sel = selectedSource < 0 && (int) i == selectedMix;
        const bool drop = dragging && (int) i == hoverMix;

        if (sel)
        {
            g.setColour (p.accent.withAlpha (0.15f));
            g.fillRoundedRectangle (r.expanded (4.0f), 14.0f);
        }

        g.setColour (p.panel);
        g.fillRoundedRectangle (r, 12.0f);

        if (drop)
        {
            juce::Path outline;
            outline.addRoundedRectangle (r.reduced (1.0f), 12.0f);
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType (2.0f).createDashedStroke (outline, outline, dashes, 2);
            g.setColour (p.accent);
            g.fillPath (outline);
        }
        else
        {
            g.setColour (sel ? p.accent : p.border);
            g.drawRoundedRectangle (r.reduced (sel ? 1.0f : 0.5f), 12.0f, sel ? 2.0f : 1.0f);
        }

        auto inner = r.reduced (14.0f, 6.0f);
        g.setFont (monoFont (12.0f));
        g.setColour (p.text);
        g.drawText (formatDb (RoutingModel::master (engine, m.ref)), inner.removeFromRight (48.0f), juce::Justification::centredRight, false);

        const bool tight = r.getHeight() < 44.0f;
        if (tight)
        {
            g.setFont (font (13.0f, true));
            g.drawText (m.name, inner, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setFont (font (14.0f, true));
            g.drawText (m.name, inner.removeFromTop (inner.getHeight() * 0.55f), juce::Justification::bottomLeft, true);
            g.setFont (font (11.5f));
            g.setColour (p.dim);
            g.drawText (m.sub, inner, juce::Justification::topLeft, true);
        }

        const auto port = juce::Rectangle<float> (14.0f, 14.0f).withCentre (portOf (m));
        g.setColour (p.panel);
        g.fillEllipse (port);
        g.setColour (drop ? p.accent : p.faint);
        g.drawEllipse (port.reduced (1.0f), 2.0f);
    }

    if (dragging)
    {
        const juce::String text = hoverMix >= 0 ? "Release to add to " + mixes[(size_t) hoverMix].name : juce::String ("Drop on a mix to add it");
        g.setFont (font (12.0f, true));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 20.0f;
        const auto pill = juce::Rectangle<float> (tw, 24.0f).withCentre (dragPos.translated (0, 22.0f));
        g.setColour (p.accent);
        g.fillRoundedRectangle (pill, 12.0f);
        g.setColour (juce::Colours::white);
        g.drawText (text, pill, juce::Justification::centred, false);
    }
}

void PatchView::mouseDown (const juce::MouseEvent& e)
{
    downPos = e.position;
    pressedSource = hitSource (e.position);
    dragging = false;
    hoverMix = -1;
}

void PatchView::mouseDrag (const juce::MouseEvent& e)
{
    if (pressedSource < 0)
        return;

    if (! dragging && e.position.getDistanceFrom (downPos) > 5.0f)
        dragging = true;

    if (dragging)
    {
        dragPos = e.position;
        hoverMix = hitMix (e.position);
        repaint();
    }
}

void PatchView::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        const int src = pressedSource >= 0 ? srcs[(size_t) pressedSource].source : -1;
        const int mix = hoverMix;
        dragging = false;
        pressedSource = -1;
        hoverMix = -1;
        repaint();

        if (src >= 0 && mix >= 0 && onRouteDropped)
            onRouteDropped (src, mix);
        return;
    }

    if (pressedSource >= 0)
    {
        const int src = srcs[(size_t) pressedSource].source;
        pressedSource = -1;
        if (onSourceClicked)
            onSourceClicked (src);
        return;
    }

    const int mix = hitMix (e.position);
    if (mix >= 0 && onMixClicked)
        onMixClicked (mix);
}

void PatchView::mouseMove (const juce::MouseEvent& e)
{
    const bool overSomething = hitSource (e.position) >= 0 || hitMix (e.position) >= 0;
    setMouseCursor (overSomething ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}
} // namespace ui

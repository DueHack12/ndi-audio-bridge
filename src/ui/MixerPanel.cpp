#include "MixerPanel.h"

namespace ui
{
//==============================================================================
class MixerPanel::SourceStrip : public juce::Component
{
public:
    SourceStrip (MixerPanel& o, int s, juce::Colour c) : owner (o), source (s), colour (c)
    {
        for (auto* b : { &onButton, &leftButton, &rightButton })
        {
            b->setFontHeight (11.0f);
            addAndMakeVisible (b);
        }
        onButton.setOnColour (pal().accent);
        leftButton.setOnColour (pal().stripSide);
        rightButton.setOnColour (pal().stripSide);
        onButton.setTooltip ("Hear this source in the selected mix");
        leftButton.setTooltip ("Left side");
        rightButton.setTooltip ("Right side");

        onButton.onClick = [this] { RoutingModel::setRouted (owner.engine, mix(), source, onButton.getToggleState()); changed(); };
        leftButton.onClick = [this] { RoutingModel::setSide (owner.engine, mix(), source, 0, leftButton.getToggleState()); changed(); };
        rightButton.onClick = [this] { RoutingModel::setSide (owner.engine, mix(), source, 1, rightButton.getToggleState()); changed(); };

        fader.setTrackColour (colour);
        fader.setTooltip ("Level of this source in the mix. Double-click for 0 dB.");
        fader.onValueChange = [this]
        {
            if (! RoutingModel::isRouted (owner.engine, mix(), source))
                RoutingModel::setRouted (owner.engine, mix(), source, true);
            RoutingModel::setLevel (owner.engine, mix(), source, (float) fader.getValue());
            changed();
        };
        addAndMakeVisible (fader);
        addAndMakeVisible (meter);
    }

    MixRef mix() const { return owner.mixes[(size_t) owner.selected]; }

    void refresh()
    {
        auto& e = owner.engine;
        const auto m = mix();
        const bool on = RoutingModel::isRouted (e, m, source);
        onButton.setToggleState (on, juce::dontSendNotification);
        onButton.setButtonText (on ? "ON" : "OFF");
        leftButton.setVisible (m.isStereo());
        rightButton.setVisible (m.isStereo());
        leftButton.setToggleState (m.isStereo() && RoutingModel::sideOn (e, m, source, 0), juce::dontSendNotification);
        rightButton.setToggleState (m.isStereo() && RoutingModel::sideOn (e, m, source, 1), juce::dontSendNotification);
        fader.setValue (RoutingModel::level (e, m, source), juce::dontSendNotification);
        fader.setAlpha (on ? 1.0f : 0.5f);
        name = e.getSourceConfig (source).name;
        device = source >= bridge::computerSource ? e.getSourceDeviceLabel (source)
                                                   : RoutingModel::shortDevice (e, bridge::interfaceOfSource (source)) + juce::String::fromUTF8 (" · ") + RoutingModel::sourceTag (source);
        resized();
        repaint();
    }

    void push (float peak) { meter.push (peak); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const bool on = onButton.getToggleState();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (p.panel2);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (p.border2);
        g.drawRoundedRectangle (r, 12.0f, 1.0f);

        auto inner = getLocalBounds().reduced (9, 9);
        g.setColour (colour.withAlpha (on ? 1.0f : 0.5f));
        g.fillRoundedRectangle (inner.removeFromTop (4).toFloat(), 2.0f);
        inner.removeFromTop (6);

        g.setColour (on ? p.text : p.dim);
        g.setFont (font (12.5f, true));
        g.drawText (name, inner.removeFromTop (17), juce::Justification::centredLeft, true);
        g.setColour (p.dim);
        g.setFont (font (10.5f));
        g.drawText (device, inner.removeFromTop (14), juce::Justification::centredLeft, true);

        g.setFont (monoFont (12.0f));
        g.setColour (on ? p.text : p.faint);
        g.drawText (on ? formatDb ((float) fader.getValue()) : juce::String ("off"),
                    getLocalBounds().reduced (9).removeFromBottom (16), juce::Justification::centred, false);
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced (9, 9);
        inner.removeFromTop (4 + 6 + 17 + 14 + 8);
        auto buttons = inner.removeFromTop (28);
        if (leftButton.isVisible())
        {
            const int side = juce::jmin (24, buttons.getWidth() / 4);
            rightButton.setBounds (buttons.removeFromRight (side));
            buttons.removeFromRight (3);
            leftButton.setBounds (buttons.removeFromRight (side));
            buttons.removeFromRight (3);
        }
        onButton.setBounds (buttons);

        inner.removeFromBottom (22);
        inner.removeFromTop (8);
        auto faderArea = inner.withSizeKeepingCentre (juce::jmin (64, inner.getWidth()), inner.getHeight());
        meter.setBounds (faderArea.removeFromLeft (6));
        faderArea.removeFromLeft (6);
        fader.setBounds (faderArea);
    }

private:
    void changed()
    {
        refresh();
        if (owner.onRoutingChanged)
            owner.onRoutingChanged();
    }

    MixerPanel& owner;
    const int source;
    const juce::Colour colour;
    juce::String name, device;
    FlatButton onButton { "ON", FlatButton::Kind::toggle };
    FlatButton leftButton { "L", FlatButton::Kind::toggle };
    FlatButton rightButton { "R", FlatButton::Kind::toggle };
    LevelSlider fader { true, -60.0, 12.0, false };
    LevelMeter meter { true };
};

//==============================================================================
class MixerPanel::MasterStrip : public juce::Component
{
public:
    explicit MasterStrip (MixerPanel& o) : owner (o)
    {
        nameEditor.setFont (font (13.0f, true));
        nameEditor.setJustification (juce::Justification::centredLeft);
        nameEditor.setIndents (6, 0);
        nameEditor.setTooltip ("Name this mix (e.g. who wears these headphones)");
        nameEditor.onReturnKey = [this] { commit(); };
        nameEditor.onFocusLost = [this] { commit(); };
        addAndMakeVisible (nameEditor);

        fader.setTrackColour (pal().text);
        fader.setTooltip ("Mix level. Double-click for 0 dB.");
        fader.onValueChange = [this]
        {
            RoutingModel::setMaster (owner.engine, owner.mixes[(size_t) owner.selected], (float) fader.getValue());
            repaint();
            if (owner.onRoutingChanged)
                owner.onRoutingChanged();
        };
        addAndMakeVisible (fader);
        addAndMakeVisible (meter);
    }

    ~MasterStrip() override
    {
        nameEditor.onFocusLost = nullptr;
        nameEditor.onReturnKey = nullptr;
    }

    void refresh()
    {
        const auto m = owner.mixes[(size_t) owner.selected];
        nameEditor.setText (RoutingModel::name (owner.engine, m), false);
        fader.setValue (RoutingModel::master (owner.engine, m), juce::dontSendNotification);
        fader.setTrackColour (pal().text);
        label = RoutingModel::outputLabel (owner.engine, m) + (m.isStereo() ? juce::String::fromUTF8 (" · stereo") : juce::String::fromUTF8 (" · mono"));
        repaint();
    }

    void push (float peak) { meter.push (peak); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto r = getLocalBounds().toFloat().reduced (0.75f);
        g.setColour (p.panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (p.text);
        g.drawRoundedRectangle (r, 12.0f, 1.5f);

        auto inner = getLocalBounds().reduced (9, 9);
        g.setColour (p.dim);
        g.setFont (font (11.0f, true));
        g.drawText ("MASTER", inner.removeFromTop (14), juce::Justification::centredLeft, false);
        inner.removeFromTop (30);
        g.setFont (font (10.5f));
        g.drawText (label, inner.removeFromTop (14), juce::Justification::centredLeft, true);

        g.setFont (monoFont (12.0f));
        g.setColour (p.text);
        g.drawText (formatDb ((float) fader.getValue()), getLocalBounds().reduced (9).removeFromBottom (16), juce::Justification::centred, false);
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced (9, 9);
        inner.removeFromTop (16);
        nameEditor.setBounds (inner.removeFromTop (26));
        inner.removeFromTop (4 + 14 + 12);
        inner.removeFromBottom (22);
        auto faderArea = inner.withSizeKeepingCentre (juce::jmin (64, inner.getWidth()), inner.getHeight());
        meter.setBounds (faderArea.removeFromLeft (6));
        faderArea.removeFromLeft (6);
        fader.setBounds (faderArea);
    }

private:
    void commit()
    {
        const auto m = owner.mixes[(size_t) owner.selected];
        auto text = nameEditor.getText().trim();
        if (text.isEmpty() || text == RoutingModel::name (owner.engine, m))
            return;
        RoutingModel::setName (owner.engine, m, text);
        owner.repaint();
        if (owner.onRoutingChanged)
            owner.onRoutingChanged();
    }

    MixerPanel& owner;
    juce::TextEditor nameEditor;
    juce::String label;
    LevelSlider fader { true, -60.0, 12.0, false };
    LevelMeter meter { true };
};

//==============================================================================
MixerPanel::MixerPanel (AudioEngine& e) : engine (e)
{
    stereoButton.setRadioGroupId (101);
    monoButton.setRadioGroupId (101);
    stereoButton.setTooltip ("This output pair is one stereo mix");
    monoButton.setTooltip ("Each output of this pair is its own mono mix (two separate headphone feeds)");

    auto setMode = [this] (bool stereo)
    {
        if (mixes.empty())
            return;
        auto& bc = engine.getBusConfig (mixes[(size_t) selected].bus);
        if (bc.stereo == stereo)
            return;
        bc.stereo = stereo;
        if (! stereo)
            bc.gainDbRight = bc.gainDb;
        engine.applyBusConfig (mixes[(size_t) selected].bus);
        if (onStructureChanged)
            onStructureChanged();
    };
    stereoButton.onClick = [setMode, this] { if (stereoButton.getToggleState()) setMode (true); };
    monoButton.onClick = [setMode, this] { if (monoButton.getToggleState()) setMode (false); };
    addAndMakeVisible (stereoButton);
    addAndMakeVisible (monoButton);

    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

MixerPanel::~MixerPanel() = default;

void MixerPanel::rebuild (const std::vector<int>& srcList, const std::vector<MixRef>& mixList,
                          std::function<juce::Colour (int)> colourOf)
{
    sources = srcList;
    mixes = mixList;
    selected = juce::jlimit (0, juce::jmax (0, (int) mixes.size() - 1), selected);

    strips.clear();
    master.reset();

    if (! mixes.empty())
    {
        for (auto s : sources)
            holder.addAndMakeVisible (strips.add (new SourceStrip (*this, s, colourOf (s))));

        master = std::make_unique<MasterStrip> (*this);
        holder.addAndMakeVisible (*master);
    }

    refreshValues();
    resized();
}

void MixerPanel::setSelectedMix (int index)
{
    if (mixes.empty())
        return;
    selected = juce::jlimit (0, (int) mixes.size() - 1, index);
    refreshValues();
    repaint();
}

void MixerPanel::refreshValues()
{
    if (mixes.empty())
        return;

    for (auto* s : strips)
        s->refresh();
    if (master != nullptr)
        master->refresh();
    updateModeButtons();
    repaint();
}

void MixerPanel::updateModeButtons()
{
    const bool show = ! mixes.empty() && engine.busHasSecondOutput (mixes[(size_t) selected].bus);
    stereoButton.setVisible (show);
    monoButton.setVisible (show);
    if (show)
    {
        const bool stereo = engine.getBusConfig (mixes[(size_t) selected].bus).stereo;
        stereoButton.setToggleState (stereo, juce::dontSendNotification);
        monoButton.setToggleState (! stereo, juce::dontSendNotification);
    }
}

void MixerPanel::updateLevels (const Levels& levels)
{
    if (mixes.empty())
        return;

    for (size_t i = 0; i < (size_t) strips.size(); ++i)
        strips[(int) i]->push (levels.source[(size_t) sources[i]]);
    if (master != nullptr)
        master->push (levels.mix (mixes[(size_t) selected]));
}

void MixerPanel::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.fillAll (p.panel);
    g.setColour (p.border);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    g.setColour (p.border2);
    g.drawHorizontalLine (52, 0.0f, (float) getWidth());

    if (mixes.empty())
    {
        g.setColour (p.dim);
        g.setFont (font (14.0f));
        g.drawText ("No headphone outputs yet. Add an audio interface with outputs.", getLocalBounds(), juce::Justification::centred);
        return;
    }

    tabRects.clear();
    std::vector<int> widths;
    int total = 0;
    for (auto& m : mixes)
    {
        const int w = (int) juce::jlimit (96.0f, 230.0f,
                                          juce::jmax (juce::GlyphArrangement::getStringWidth (font (13.5f, true), RoutingModel::name (engine, m)),
                                                      juce::GlyphArrangement::getStringWidth (font (11.0f), RoutingModel::subtitle (engine, m))) + 26.0f);
        widths.push_back (w);
        total += w + 2;
    }

    const int available = getWidth() - 20 - (stereoButton.isVisible() ? 200 : 20);
    const float squeeze = total > available ? (float) available / (float) total : 1.0f;

    g.saveState();
    g.reduceClipRegion (0, 0, 20 + available, 53);
    int x = 20;
    for (size_t i = 0; i < mixes.size(); ++i)
    {
        const auto name = RoutingModel::name (engine, mixes[i]);
        const auto sub = RoutingModel::subtitle (engine, mixes[i]);
        const int w = juce::jmax (72, (int) ((float) widths[i] * squeeze));
        const auto r = juce::Rectangle<int> (x, 1, w, 51);
        tabRects.push_back (r);
        x += w + 2;

        const bool sel = (int) i == selected;
        g.setColour (p.text);
        g.setFont (font (13.5f, true));
        g.drawText (name, r.reduced (12, 0).withTrimmedTop (9).withHeight (18), juce::Justification::centredLeft, true);
        g.setColour (p.dim);
        g.setFont (font (11.0f));
        g.drawText (sub, r.reduced (12, 0).withTrimmedTop (27).withHeight (15), juce::Justification::centredLeft, true);

        if (sel)
        {
            g.setColour (p.accent);
            g.fillRect (r.getX() + 4, 49, r.getWidth() - 8, 3);
        }
    }
    g.restoreState();
}

void MixerPanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (52);
    auto modes = header.removeFromRight (190).reduced (20, 12);
    monoButton.setBounds (modes.removeFromRight (modes.getWidth() / 2));
    stereoButton.setBounds (modes);

    viewport.setBounds (r.reduced (20, 12));
    layoutStrips();
}

void MixerPanel::layoutStrips()
{
    const int n = strips.size();
    if (n == 0 || master == nullptr)
    {
        holder.setSize (0, 0);
        return;
    }

    const int gap = 8;
    const int height = viewport.getHeight() - (viewport.getHorizontalScrollBar().isVisible() ? 10 : 0);
    const int available = viewport.getWidth();
    const int masterW = 140;
    const int stripW = juce::jmax (80, (available - masterW - gap * n) / n);
    const int total = n * (stripW + gap) + masterW;

    holder.setSize (juce::jmax (total, available), juce::jmax (0, height));
    int x = 0;
    for (auto* s : strips)
    {
        s->setBounds (x, 0, stripW, holder.getHeight());
        x += stripW + gap;
    }
    master->setBounds (x, 0, masterW, holder.getHeight());
}

void MixerPanel::mouseUp (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < tabRects.size(); ++i)
    {
        if (tabRects[i].contains (e.getPosition()))
        {
            setSelectedMix ((int) i);
            if (onMixSelected)
                onMixSelected ((int) i);
            return;
        }
    }
}
} // namespace ui

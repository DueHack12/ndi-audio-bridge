#include "MainComponent.h"
#include "engine/Settings.h"
#include "ndi/NdiLibrary.h"

using namespace bridge;

namespace theme
{
const juce::Colour bg        { 0xff111317 };
const juce::Colour panel     { 0xff1a1d23 };
const juce::Colour panelEdge { 0xff2a2f38 };
const juce::Colour text      { 0xffe7e9ec };
const juce::Colour dim       { 0xff8b929c };
const juce::Colour off       { 0xff2a2f38 };
const juce::Colour send      { 0xff22a55b };
const juce::Colour mute      { 0xffd93f45 };
const juce::Colour route     { 0xff2f7cf6 };
const juce::Colour warn      { 0xfff0a020 };
} // namespace theme

static juce::Font uiFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

static void styleToggle (juce::TextButton& b, juce::Colour onColour)
{
    b.setClickingTogglesState (true);
    b.setColour (juce::TextButton::buttonColourId, theme::off);
    b.setColour (juce::TextButton::buttonOnColourId, onColour);
    b.setColour (juce::TextButton::textColourOffId, theme::dim);
    b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

static void styleGainSlider (juce::Slider& s, double minDb = -60.0, double maxDb = 24.0)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    s.setRange (minDb, maxDb, 0.5);
    s.setDoubleClickReturnValue (true, 0.0);
    s.textFromValueFunction = [minDb] (double v) { return v <= minDb ? juce::String ("Off") : juce::String (v, 1) + " dB"; };
    s.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::backgroundColourId, theme::panelEdge);
    s.updateText();
}

static void styleMsSlider (juce::Slider& s, double minMs, double maxMs)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    s.setRange (minMs, maxMs, 1.0);
    s.setTextValueSuffix (" ms");
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::backgroundColourId, theme::panelEdge);
}

static void styleEditor (juce::TextEditor& e)
{
    e.setFont (uiFont (14.0f));
    e.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0f12));
    e.setColour (juce::TextEditor::outlineColourId, theme::panelEdge);
    e.setIndents (6, 0);
    e.setJustification (juce::Justification::centredLeft);
}

//==============================================================================
class LevelMeter : public juce::Component
{
public:
    void push (float linearPeak)
    {
        const float db = juce::Decibels::gainToDecibels (linearPeak, -60.0f);
        level = db > level ? db : juce::jmax (db, level - 1.8f);

        if (db >= hold) { hold = db; holdTicks = 40; }
        else if (--holdTicks <= 0) hold = juce::jmax (-60.0f, hold - 1.8f);

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0c0f));
        g.fillRoundedRectangle (r, 2.0f);

        auto toX = [&r] (float db) { return r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };

        const float x = toX (level);
        juce::ColourGradient grad (juce::Colour (0xff22a55b), r.getX(), 0, juce::Colour (0xffd93f45), r.getRight(), 0, false);
        grad.addColour (0.75, juce::Colour (0xff9ccc3c));
        grad.addColour (0.9, juce::Colour (0xfff0a020));
        g.setGradientFill (grad);
        g.fillRect (r.withRight (x));

        if (hold > -59.0f)
        {
            g.setColour (hold > -0.5f ? theme::mute : theme::text.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (toX (hold) - 1.0f, r.getY(), 2.0f, r.getHeight()));
        }
    }

private:
    float level = -60.0f, hold = -60.0f;
    int holdTicks = 0;
};

//==============================================================================
/** One NDI send: an interface input or the computer audio. */
class SendRow : public juce::Component
{
public:
    static constexpr int rowHeight = 36;
    enum Column { chan, on, name, gain, meter, stereo, mute, status, numColumns };

    static std::array<juce::Rectangle<int>, numColumns> columns (juce::Rectangle<int> r)
    {
        std::array<juce::Rectangle<int>, numColumns> c;
        c[chan]   = r.removeFromLeft (52);
        c[on]     = r.removeFromLeft (62);
        c[status] = r.removeFromRight (112);
        c[mute]   = r.removeFromRight (62);
        c[stereo] = r.removeFromRight (72);
        c[meter]  = r.removeFromRight (90);
        c[gain]   = r.removeFromRight (160);
        c[name]   = r;
        return c;
    }

    SendRow (AudioEngine& e, int sourceIndex, std::function<void()> onChange, std::function<void()> onNameChange)
        : engine (e), source (sourceIndex), changed (std::move (onChange)), nameChanged (std::move (onNameChange))
    {
        channelLabel.setFont (uiFont (13.0f, true));
        channelLabel.setColour (juce::Label::textColourId, theme::dim);
        addAndMakeVisible (channelLabel);

        onButton.setButtonText ("SEND");
        styleToggle (onButton, theme::send);
        onButton.setTooltip ("Publish this as an NDI source on the network");
        onButton.onClick = [this] { config().sendEnabled = onButton.getToggleState(); apply(); };
        addAndMakeVisible (onButton);

        styleEditor (nameEditor);
        nameEditor.setTooltip ("NDI source name (vMix shows it as COMPUTERNAME (name))");
        nameEditor.onReturnKey = [this] { commitName(); };
        nameEditor.onFocusLost = [this] { commitName(); };
        addAndMakeVisible (nameEditor);

        styleGainSlider (gainSlider);
        gainSlider.setTooltip ("Level for this source (applies to the NDI send and the headphone mixes). Double-click for 0 dB.");
        gainSlider.onValueChange = [this] { config().gainDb = (float) gainSlider.getValue(); apply(); };
        addAndMakeVisible (gainSlider);

        addAndMakeVisible (meterComp);

        styleToggle (stereoButton, theme::route.darker (0.3f));
        stereoButton.setTooltip ("On: 2-channel NDI source (mic inputs are sent dual-mono). Off: 1-channel mono source.");
        stereoButton.onClick = [this] { config().sendStereo = stereoButton.getToggleState(); updateStereoText(); apply(); };
        addAndMakeVisible (stereoButton);

        muteButton.setButtonText ("MUTE");
        styleToggle (muteButton, theme::mute);
        muteButton.setTooltip ("Cough button: silences the NDI send to vMix. Headphone mixes are not affected.");
        muteButton.onClick = [this] { config().sendMuted = muteButton.getToggleState(); apply(); };
        addAndMakeVisible (muteButton);

        statusLabel.setFont (uiFont (12.5f));
        addAndMakeVisible (statusLabel);

        refreshFromConfig();
    }

    ~SendRow() override
    {
        // Don't let a focused editor commit while the row is being torn down.
        nameEditor.onFocusLost = nullptr;
        nameEditor.onReturnKey = nullptr;
    }

    void refreshFromConfig()
    {
        auto& c = config();
        channelLabel.setText (source == computerSource ? juce::String ("PC") : "IN " + juce::String (source + 1),
                              juce::dontSendNotification);
        channelLabel.setTooltip (source == computerSource ? juce::String ("Computer audio (virtual output device)")
                                                          : engine.getInputChannelName (source));
        onButton.setToggleState (c.sendEnabled, juce::dontSendNotification);
        nameEditor.setText (c.name, false);
        gainSlider.setValue (c.gainDb, juce::dontSendNotification);
        stereoButton.setToggleState (c.sendStereo, juce::dontSendNotification);
        muteButton.setToggleState (c.sendMuted, juce::dontSendNotification);
        updateStereoText();
    }

    void tick()
    {
        meterComp.push (engine.getSourceMeter (source).readAndReset());

        auto& slot = engine.getSendHub().getSlot (source);
        juce::String text;
        juce::Colour colour = theme::dim;

        if (! ndi::lib().isLoaded())                                    { text = "NDI missing"; colour = theme::mute; }
        else if (source == computerSource && ! engine.isComputerAudioRunning()) { text = "No device"; }
        else if (! config().sendEnabled)                                 { text = "Not sent"; }
        else if (! slot.live.load())                                     { text = "Starting..."; colour = theme::warn; }
        else
        {
            const int n = slot.connections.load();
            text = n == 0 ? juce::String ("Live, 0 receivers") : "Live, " + juce::String (n) + (n == 1 ? " receiver" : " receivers");
            colour = n == 0 ? theme::warn : theme::send.brighter (0.4f);
        }

        if (config().sendMuted && config().sendEnabled)
        {
            text = "MUTED";
            colour = theme::mute;
        }

        if (statusLabel.getText() != text)
            statusLabel.setText (text, juce::dontSendNotification);

        statusLabel.setColour (juce::Label::textColourId, colour);
    }

    void resized() override
    {
        auto c = columns (getLocalBounds());
        auto pad = [] (juce::Rectangle<int> r) { return r.reduced (3, 4); };

        channelLabel.setBounds (c[chan]);
        onButton.setBounds (pad (c[on]));
        nameEditor.setBounds (pad (c[name]));
        gainSlider.setBounds (pad (c[gain]));
        meterComp.setBounds (c[meter].reduced (4, 13));
        stereoButton.setBounds (pad (c[stereo]));
        muteButton.setBounds (pad (c[mute]));
        statusLabel.setBounds (c[status].withTrimmedLeft (6));
    }

private:
    SourceConfig& config() { return engine.getSourceConfig (source); }

    void apply()
    {
        engine.applySourceConfig (source);
        changed();
    }

    void commitName()
    {
        auto newName = nameEditor.getText().trim();
        if (newName.isEmpty())
            newName = AudioEngine::defaultSourceName (source);

        // NDI source names must be unique on this machine.
        for (int s = 0; s < numSources; ++s)
            if (s != source && s != returnSource && engine.getSourceConfig (s).name == newName)
                newName << " " << (source + 1);

        if (newName != config().name)
        {
            config().name = newName;
            apply();
            nameChanged();
        }

        nameEditor.setText (newName, false);
    }

    void updateStereoText()
    {
        stereoButton.setButtonText (stereoButton.getToggleState() ? "STEREO" : "MONO");
    }

    AudioEngine& engine;
    const int source;
    std::function<void()> changed, nameChanged;

    juce::Label channelLabel;
    juce::TextButton onButton, stereoButton, muteButton;
    juce::TextEditor nameEditor;
    juce::Slider gainSlider;
    LevelMeter meterComp;
    juce::Label statusLabel;
};

//==============================================================================
/** The headphone matrix: rows = sources, columns = output channels, grouped in pairs.

    Each pair (Out 1/2, Out 3/4, ...) is either one STEREO mix or 2x MONO (two
    independent mixes). Every cell routes a source to one output channel, so
    left and right are always controllable separately. */
class MatrixPanel : public juce::Component
{
public:
    MatrixPanel (AudioEngine& e, std::function<void()> onChange) : engine (e), changed (std::move (onChange)) {}

    ~MatrixPanel() override { detachEditors(); }

    void rebuild (int numInputs, int numBuses)
    {
        detachEditors();
        pairs.clear();
        rowLabels.clear();
        rowSources.clear();

        for (int i = 0; i < numInputs; ++i) rowSources.push_back (i);
        rowSources.push_back (computerSource);
        rowSources.push_back (returnSource);

        for (int b = 0; b < numBuses; ++b)
            buildPair (b);

        for (auto s : rowSources)
        {
            auto* l = rowLabels.add (new juce::Label());
            l->setFont (uiFont (14.0f, s >= maxInputs));
            l->setColour (juce::Label::textColourId, theme::text);
            addAndMakeVisible (l);
        }

        refreshNames();
        resized();
    }

    void refreshNames()
    {
        for (size_t r = 0; r < rowSources.size(); ++r)
        {
            const int s = rowSources[r];
            juce::String label = engine.getSourceConfig (s).name;
            if (s < maxInputs)
                label = "IN " + juce::String (s + 1) + "   " + label;
            rowLabels[(int) r]->setText (label, juce::dontSendNotification);
        }

        for (auto* p : pairs)
            updateCellTooltips (*p);
    }

    void tick()
    {
        for (int b = 0; b < pairs.size(); ++b)
        {
            pairs[b]->side[0].meter.push (engine.getBusMeter (b, 0).readAndReset());
            pairs[b]->side[1].meter.push (engine.getBusMeter (b, 1).readAndReset());
        }
    }

    void paint (juce::Graphics& g) override
    {
        for (size_t r = 0; r < rowSources.size(); ++r)
        {
            auto row = rowBounds ((int) r);
            if (rowSources[r] == computerSource)
            {
                g.setColour (theme::panelEdge);
                g.drawHorizontalLine (row.getY() - 3, (float) row.getX(), (float) row.getRight());
            }
            if (r % 2 == 1)
            {
                g.setColour (juce::Colours::white.withAlpha (0.025f));
                g.fillRect (row);
            }
        }

        // Dividers between output pairs
        g.setColour (theme::panelEdge);
        for (int b = 1; b < pairs.size(); ++b)
        {
            const int x = pairBounds (b).getX();
            g.drawVerticalLine (x, 4.0f, (float) getHeight() - 4.0f);
        }
    }

    void resized() override
    {
        if (pairs.isEmpty())
            return;

        for (int b = 0; b < pairs.size(); ++b)
        {
            auto& p = *pairs[b];
            const bool split = ! p.stereo();
            auto area = pairBounds (b).withHeight (headerHeight).reduced (6, 0);

            auto modeRow = area.removeFromTop (24);
            p.stereoButton.setVisible (p.hasRight);
            p.monoButton.setVisible (p.hasRight);
            p.stereoButton.setBounds (modeRow.removeFromLeft (modeRow.getWidth() / 2).withTrimmedRight (1));
            p.monoButton.setBounds (modeRow.withTrimmedLeft (1));
            area.removeFromTop (6);

            auto layoutSide = [] (Side& side, juce::Rectangle<int> col, bool meterOnly)
            {
                if (! meterOnly)
                {
                    side.name.setBounds (col.removeFromTop (26));
                    side.channels.setBounds (col.removeFromTop (18));
                    side.gain.setBounds (col.removeFromTop (40));
                }
                else
                {
                    col.removeFromTop (26 + 18 + 40);
                }
                col.removeFromTop (5);
                return col;
            };

            if (split)
            {
                auto left = area.removeFromLeft (area.getWidth() / 2).withTrimmedRight (3);
                auto right = area.withTrimmedLeft (3);
                p.side[0].meter.setBounds (layoutSide (p.side[0], left, false).removeFromTop (8));
                p.side[1].meter.setBounds (layoutSide (p.side[1], right, false).removeFromTop (8));
            }
            else
            {
                auto rest = layoutSide (p.side[0], area, false);
                p.side[0].meter.setBounds (rest.removeFromTop (5));
                rest.removeFromTop (2);
                p.side[1].meter.setBounds (rest.removeFromTop (5));
            }

            for (int sd = 0; sd < 2; ++sd)
            {
                auto& side = p.side[sd];
                const bool showControls = sd == 0 || (split && p.hasRight);
                side.name.setVisible (showControls);
                side.channels.setVisible (showControls);
                side.gain.setVisible (showControls);
                side.meter.setVisible (sd == 0 || p.hasRight);
            }
        }

        for (size_t row = 0; row < rowSources.size(); ++row)
        {
            auto rb = rowBounds ((int) row);
            rowLabels[(int) row]->setBounds (rb.withWidth (labelWidth).withTrimmedLeft (4));

            for (int b = 0; b < pairs.size(); ++b)
            {
                auto& p = *pairs[b];
                auto cellsArea = pairBounds (b).withY (rb.getY()).withHeight (rb.getHeight()).reduced (6, 4);
                auto leftCell = cellsArea.removeFromLeft (cellsArea.getWidth() / 2).withTrimmedRight (2);
                auto rightCell = cellsArea.withTrimmedLeft (2);

                p.cells[0][row]->setBounds (leftCell);
                p.cells[1][row]->setBounds (rightCell);
                p.cells[1][row]->setVisible (p.hasRight);
            }
        }
    }

private:
    static constexpr int headerHeight = 132, rowHeight = 42, labelWidth = 150;

    struct Side
    {
        juce::TextEditor name;
        juce::Label channels;
        juce::Slider gain;
        LevelMeter meter;
    };

    struct Pair
    {
        int bus = 0;
        bool hasRight = true;
        juce::TextButton stereoButton { "STEREO" }, monoButton { juce::String::fromUTF8 ("2\xc3\x97 MONO") };
        Side side[2];
        juce::OwnedArray<juce::TextButton> cells[2]; // [side][row]
        BusConfig* config = nullptr;

        bool stereo() const { return hasRight && config->stereo; }
    };

    juce::Rectangle<int> pairBounds (int b) const
    {
        const int n = juce::jmax (1, pairs.size());
        const int width = (getWidth() - labelWidth) / n;
        return { labelWidth + b * width, 0, width, getHeight() };
    }

    juce::Rectangle<int> rowBounds (int row) const
    {
        const int extra = rowSources[(size_t) row] >= maxInputs ? 6 : 0; // gap before computer/return rows
        int y = headerHeight + 8 + row * rowHeight;
        if (rowSources[(size_t) row] == returnSource) y += 6;
        return { 0, y + extra, getWidth(), rowHeight };
    }

    void buildPair (int b)
    {
        auto* p = pairs.add (new Pair());
        p->bus = b;
        p->config = &engine.getBusConfig (b);
        p->hasRight = engine.busHasSecondOutput (b);
        auto& bc = *p->config;

        // STEREO | 2x MONO selector
        for (auto* btn : { &p->stereoButton, &p->monoButton })
        {
            styleToggle (*btn, btn == &p->stereoButton ? theme::route.darker (0.35f) : theme::warn.darker (0.35f));
            btn->setRadioGroupId (1000 + b, juce::dontSendNotification);
            btn->setConnectedEdges (btn == &p->stereoButton ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
            addAndMakeVisible (btn);
        }
        p->stereoButton.setTooltip ("One stereo mix on this output pair (e.g. a stereo headphone feed)");
        p->monoButton.setTooltip ("Two separate mono mixes, one per output (e.g. two different headphone feeds)");
        p->stereoButton.setToggleState (bc.stereo, juce::dontSendNotification);
        p->monoButton.setToggleState (! bc.stereo, juce::dontSendNotification);

        auto setMode = [this, p] (bool stereo)
        {
            if (p->config->stereo == stereo)
                return;
            p->config->stereo = stereo;
            if (! stereo)
                p->config->gainDbRight = p->config->gainDb; // start the second mix at the same level
            engine.applyBusConfig (p->bus);
            refreshPairTexts (*p);
            resized();
            repaint();
            changed();
        };
        p->stereoButton.onClick = [p, setMode] { if (p->stereoButton.getToggleState()) setMode (true); };
        p->monoButton.onClick   = [p, setMode] { if (p->monoButton.getToggleState()) setMode (false); };

        for (int sd = 0; sd < 2; ++sd)
        {
            auto& side = p->side[sd];

            styleEditor (side.name);
            side.name.setFont (uiFont (13.0f, true));
            side.name.setJustification (juce::Justification::centred);
            side.name.setTooltip ("Name this mix (e.g. who wears these headphones)");
            auto commit = [this, p, sd]
            {
                auto text = p->side[sd].name.getText().trim();
                (sd == 0 ? p->config->name : p->config->nameRight) = text;
                updateCellTooltips (*p);
                changed();
            };
            side.name.onReturnKey = commit;
            side.name.onFocusLost = commit;
            addAndMakeVisible (side.name);

            side.channels.setJustificationType (juce::Justification::centred);
            side.channels.setFont (uiFont (12.5f, true));
            side.channels.setColour (juce::Label::textColourId, theme::dim);
            addAndMakeVisible (side.channels);

            styleGainSlider (side.gain, -60.0, 12.0);
            side.gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            side.gain.setTooltip ("Mix level. Double-click for 0 dB.");
            side.gain.onValueChange = [this, p, sd]
            {
                (sd == 0 ? p->config->gainDb : p->config->gainDbRight) = (float) p->side[sd].gain.getValue();
                engine.applyBusConfig (p->bus);
                changed();
            };
            addAndMakeVisible (side.gain);
            addAndMakeVisible (side.meter);

            for (size_t row = 0; row < rowSources.size(); ++row)
            {
                const int s = rowSources[row];
                auto* cell = p->cells[sd].add (new juce::TextButton());
                styleToggle (*cell, theme::route);
                cell->setToggleState (bc.routes[sd][s], juce::dontSendNotification);
                cell->onClick = [this, p, sd, s, row]
                {
                    const bool on = p->cells[sd][(int) row]->getToggleState();
                    p->config->routes[sd][s] = on;

                    // Shift-click sets both sides at once.
                    if (juce::ModifierKeys::currentModifiers.isShiftDown() && p->hasRight)
                    {
                        p->config->routes[1 - sd][s] = on;
                        p->cells[1 - sd][(int) row]->setToggleState (on, juce::dontSendNotification);
                    }

                    engine.applyBusConfig (p->bus);
                    refreshPairTexts (*p);
                    changed();
                };
                addAndMakeVisible (cell);
            }
        }

        refreshPairTexts (*p);
    }

    void refreshPairTexts (Pair& p)
    {
        const bool split = ! p.stereo();
        auto& bc = *p.config;

        for (auto& side : p.side)
        {
            side.name.setIndents (split ? 2 : 6, 0);
            side.name.applyFontToAllText (uiFont (split ? 12.0f : 13.0f, true));
        }

        p.side[0].name.setText (bc.name, false);
        p.side[1].name.setText (bc.nameRight, false);
        p.side[0].gain.setValue (bc.gainDb, juce::dontSendNotification);
        p.side[1].gain.setValue (bc.gainDbRight, juce::dontSendNotification);

        if (split || ! p.hasRight)
        {
            p.side[0].channels.setText (engine.getOutputChannelLabel (p.bus, 0), juce::dontSendNotification);
            p.side[1].channels.setText (engine.getOutputChannelLabel (p.bus, 1), juce::dontSendNotification);
        }
        else
        {
            p.side[0].channels.setText (engine.getBusChannelLabel (p.bus) + "  (L / R)", juce::dontSendNotification);
        }

        for (int sd = 0; sd < 2; ++sd)
        {
            for (auto* cell : p.cells[sd])
            {
                const bool on = cell->getToggleState();
                cell->setButtonText (split ? (on ? "ON" : "-") : juce::String (sd == 0 ? "L" : "R"));
            }
        }

        updateCellTooltips (p);
    }

    void updateCellTooltips (Pair& p)
    {
        const bool split = ! p.stereo();

        for (int sd = 0; sd < 2; ++sd)
        {
            for (size_t row = 0; row < rowSources.size() && row < (size_t) p.cells[sd].size(); ++row)
            {
                const auto source = engine.getSourceConfig (rowSources[row]).name;
                const auto dest = split ? (sd == 0 ? p.config->name : p.config->nameRight) + " (" + engine.getOutputChannelLabel (p.bus, sd) + ")"
                                        : juce::String (sd == 0 ? "the LEFT side of " : "the RIGHT side of ") + p.config->name;
                p.cells[sd][(int) row]->setTooltip ("Send " + source + " to " + dest
                                                    + (p.hasRight ? ". Shift-click sets both sides." : "."));
            }
        }
    }

    void detachEditors()
    {
        for (auto* p : pairs)
            for (auto& side : p->side)
            {
                side.name.onFocusLost = nullptr;
                side.name.onReturnKey = nullptr;
            }
    }

    AudioEngine& engine;
    std::function<void()> changed;
    juce::OwnedArray<Pair> pairs;
    juce::OwnedArray<juce::Label> rowLabels;
    std::vector<int> rowSources;
};

//==============================================================================
MainComponent::MainComponent (AudioEngine& e) : engine (e)
{
    interfaceButton.onClick = [this] { showInterfaceSettings(); };
    addAndMakeVisible (interfaceButton);

    interfaceSummary.setFont (uiFont (13.0f));
    interfaceSummary.setColour (juce::Label::textColourId, theme::dim);
    addAndMakeVisible (interfaceSummary);

    // vMix return
    returnCombo.setTextWhenNothingSelected ("Choose the vMix NDI output...");
    returnCombo.setTooltip ("NDI source to play into the headphone mixes (e.g. a vMix output or bus)");
    returnCombo.onChange = [this]
    {
        const auto id = returnCombo.getSelectedId();
        engine.setReturnSource (id <= 1 ? juce::String() : returnCombo.getText());
        markDirty();
    };
    addAndMakeVisible (returnCombo);

    styleGainSlider (returnGain);
    returnGain.setValue (engine.getSourceConfig (returnSource).gainDb, juce::dontSendNotification);
    returnGain.onValueChange = [this]
    {
        engine.getSourceConfig (returnSource).gainDb = (float) returnGain.getValue();
        engine.applySourceConfig (returnSource);
        markDirty();
    };
    addAndMakeVisible (returnGain);

    styleMsSlider (returnBuffer, 10.0, 300.0);
    returnBuffer.setValue (engine.getReturnBufferMs(), juce::dontSendNotification);
    returnBuffer.setTooltip ("Jitter buffer for the network return. Raise it if you hear dropouts; lower it for less delay.");
    returnBuffer.onValueChange = [this] { engine.setReturnBufferMs (returnBuffer.getValue()); markDirty(); };
    addAndMakeVisible (returnBuffer);
    returnBufferLabel.setText ("Buffer", juce::dontSendNotification);
    returnBufferLabel.setColour (juce::Label::textColourId, theme::dim);
    returnBufferLabel.attachToComponent (&returnBuffer, true);

    returnMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible (*returnMeter);

    returnStatus.setFont (uiFont (13.0f));
    addAndMakeVisible (returnStatus);

    // Computer audio
    computerCombo.setTextWhenNothingSelected ("Off");
    computerCombo.setTooltip ("The virtual audio device your computer plays into");
    computerCombo.onChange = [this]
    {
        const auto id = computerCombo.getSelectedId();
        const auto err = engine.openComputerAudioDevice (id <= 1 ? juce::String() : computerCombo.getText());
        if (err.isNotEmpty())
            computerStatus.setText (err, juce::dontSendNotification);
        markDirty();
    };
    addAndMakeVisible (computerCombo);

    computerRescan.onClick = [this] { refreshComputerDevices(); };
    addAndMakeVisible (computerRescan);

    styleMsSlider (computerBuffer, 10.0, 200.0);
    computerBuffer.setValue (engine.getComputerBufferMs(), juce::dontSendNotification);
    computerBuffer.setTooltip ("Buffer used when computer audio is mixed into the headphones");
    computerBuffer.onValueChange = [this] { engine.setComputerBufferMs (computerBuffer.getValue()); markDirty(); };
    addAndMakeVisible (computerBuffer);
    computerBufferLabel.setText ("Buffer", juce::dontSendNotification);
    computerBufferLabel.setColour (juce::Label::textColourId, theme::dim);
    computerBufferLabel.attachToComponent (&computerBuffer, true);

    computerStatus.setFont (uiFont (13.0f));
    addAndMakeVisible (computerStatus);

   #if JUCE_MAC
    computerHint.setText ("Pick \"NDI Audio Bridge\" (or BlackHole) here, then choose the same device in "
                          "System Settings > Sound > Output.", juce::dontSendNotification);
   #elif JUCE_WINDOWS
    computerHint.setText ("Pick \"CABLE Output\" here, then set Windows' default playback device to \"CABLE Input\" "
                          "(VB-Audio Virtual Cable).", juce::dontSendNotification);
   #else
    computerHint.setText ("Pick a loopback capture device here.", juce::dontSendNotification);
   #endif
    computerHint.setFont (uiFont (12.5f));
    computerHint.setColour (juce::Label::textColourId, theme::dim);
    computerHint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (computerHint);

    sendViewport.setViewedComponent (&sendRowsHolder, false);
    sendViewport.setScrollBarsShown (true, false);
    sendViewport.setScrollBarThickness (8);
    addAndMakeVisible (sendViewport);

    matrix = std::make_unique<MatrixPanel> (engine, [this] { markDirty(); });
    addAndMakeVisible (*matrix);

    refreshComputerDevices();
    refreshReturnSources (true);
    rebuildForDevice();

    setSize (1320, 820);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::markDirty()
{
    dirty = true;
    lastChangeMs = juce::Time::getMillisecondCounter();
}

void MainComponent::saveNow()
{
    Settings::save (engine);
    dirty = false;
}

void MainComponent::rebuildForDevice()
{
    builtInputs = engine.getNumInputs();
    builtBuses = engine.getNumBuses();

    sendRows.clear();

    auto addRow = [this] (int source)
    {
        auto* row = sendRows.add (new SendRow (engine, source, [this] { markDirty(); }, [this] { sourceNamesChanged(); }));
        sendRowsHolder.addAndMakeVisible (row);
    };

    for (int i = 0; i < builtInputs; ++i)
        addRow (i);

    addRow (computerSource);

    matrix->rebuild (builtInputs, builtBuses);
    resized();
    repaint();
}

void MainComponent::sourceNamesChanged()
{
    matrix->refreshNames();
}

void MainComponent::showInterfaceSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getInterfaceManager(),
                                                             0, maxInputs, 0, maxBuses * 2,
                                                             false, false, false, false);
    selector->setSize (560, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio Interface";
    o.dialogBackgroundColour = theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::refreshReturnSources (bool force)
{
    auto& finder = engine.getFinder();
    const int change = finder.getChangeCount();

    if (! force && (change == lastFinderChange || returnCombo.isPopupActive()))
        return;

    lastFinderChange = change;

    auto sources = finder.getSources();
    const auto current = engine.getReturnSource();

    // Keep the saved choice in the list even while it is offline.
    if (current.isNotEmpty() && ! sources.contains (current))
        sources.insert (0, current);

    returnCombo.clear (juce::dontSendNotification);
    returnCombo.addItem ("(None)", 1);

    for (int i = 0; i < sources.size(); ++i)
        returnCombo.addItem (sources[i], i + 2);

    returnCombo.setSelectedId (current.isEmpty() ? 1 : sources.indexOf (current) + 2, juce::dontSendNotification);
}

void MainComponent::refreshComputerDevices()
{
    auto names = engine.getComputerAudioDeviceNames();
    const auto current = engine.getComputerAudioDeviceName();

    if (current.isNotEmpty() && ! names.contains (current))
        names.insert (0, current);

    computerCombo.clear (juce::dontSendNotification);
    computerCombo.addItem ("(Off)", 1);

    for (int i = 0; i < names.size(); ++i)
        computerCombo.addItem (names[i], i + 2);

    computerCombo.setSelectedId (current.isEmpty() ? 1 : names.indexOf (current) + 2, juce::dontSendNotification);
}

void MainComponent::updateStatusText()
{
    const auto summary = engine.getInterfaceSummary();
    if (interfaceSummary.getText() != summary)
    {
        if (interfaceSummary.getText().isNotEmpty())
            markDirty(); // device changed via the settings dialog
        interfaceSummary.setText (summary, juce::dontSendNotification);
    }

    // Return status
    auto& rx = engine.getReceiver();
    auto& fifo = engine.getReturnFifo();
    juce::String text;
    juce::Colour colour = theme::dim;

    if (! ndi::lib().isLoaded())
    {
        text = "NDI runtime not installed";
        colour = theme::mute;
    }
    else if (engine.getReturnSource().isEmpty())
    {
        text = "Not monitoring a return feed";
    }
    else if (rx.isReceiving())
    {
        text = "Receiving " + juce::String (rx.getSourceChannels()) + " ch @ "
               + juce::String (rx.getSourceSampleRate() / 1000.0, 1) + " kHz  |  buffer "
               + juce::String (fifo.getBufferedMs(), 0) + " ms  |  drift "
               + juce::String (fifo.getCorrectionPpm(), 0) + " ppm  |  dropouts "
               + juce::String (fifo.getUnderruns());
        colour = theme::send.brighter (0.4f);
    }
    else
    {
        text = rx.isConnected() ? "Connected, waiting for audio..." : "Looking for source...";
        colour = theme::warn;
    }

    if (returnStatus.getText() != text)
        returnStatus.setText (text, juce::dontSendNotification);
    returnStatus.setColour (juce::Label::textColourId, colour);

    // Computer status
    const auto compText = engine.isComputerAudioRunning()
                              ? engine.getComputerAudioSummary() + "  |  headphone buffer "
                                    + juce::String (engine.getComputerFifo().getBufferedMs(), 0) + " ms"
                              : engine.getComputerAudioSummary();
    if (computerStatus.getText() != compText)
        computerStatus.setText (compText, juce::dontSendNotification);
    computerStatus.setColour (juce::Label::textColourId, engine.isComputerAudioRunning() ? theme::send.brighter (0.4f) : theme::dim);
}

void MainComponent::timerCallback()
{
    if (engine.getNumInputs() != builtInputs || engine.getNumBuses() != builtBuses)
        rebuildForDevice();

    for (auto* row : sendRows)
        row->tick();

    matrix->tick();
    returnMeter->push (engine.getSourceMeter (returnSource).readAndReset());

    refreshReturnSources (false);
    updateStatusText();

    const auto now = juce::Time::getMillisecondCounter();

    // Reconnect the computer audio device if it was unplugged / not installed yet.
    if (engine.getComputerAudioDeviceName().isNotEmpty() && ! engine.isComputerAudioRunning()
        && now - lastComputerRetryMs > 5000)
    {
        lastComputerRetryMs = now;
        if (engine.openComputerAudioDevice (engine.getComputerAudioDeviceName()).isEmpty())
            refreshComputerDevices();
    }

    if (dirty && now - lastChangeMs > 1000)
        saveNow();
}

//==============================================================================
static void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& subtitle = {})
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (theme::panelEdge);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);

    auto titleArea = area.reduced (14, 0).removeFromTop (34);
    g.setColour (theme::text);
    g.setFont (uiFont (13.5f, true));
    g.drawText (title, titleArea, juce::Justification::centredLeft);

    if (subtitle.isNotEmpty())
    {
        g.setColour (theme::dim);
        g.setFont (uiFont (12.5f));
        g.drawText (subtitle, titleArea.withTrimmedLeft ((int) juce::GlyphArrangement::getStringWidth (uiFont (13.5f, true), title) + 12),
                    juce::Justification::centredLeft);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    // Header
    auto h = headerArea;
    g.setColour (theme::text);
    g.setFont (uiFont (22.0f, true));
    g.drawText ("NDI Audio Bridge", h.removeFromTop (32), juce::Justification::centredLeft);

    // NDI status pill
    {
        const bool ok = ndi::lib().isLoaded();
        const auto label = ok ? "NDI ready  (v" + ndi::lib().getStatusText().fromLastOccurrenceOf (" ", false, false) + ")"
                              : ndi::lib().getStatusText();
        auto font = uiFont (12.5f, true);
        const int w = juce::jmin (headerArea.getWidth() / 2, (int) juce::GlyphArrangement::getStringWidth (font, label) + 24);
        auto pill = juce::Rectangle<int> (headerArea.getX() + 230, headerArea.getY() + 5, w, 22);
        g.setColour ((ok ? theme::send : theme::mute).withAlpha (0.2f));
        g.fillRoundedRectangle (pill.toFloat(), 11.0f);
        g.setColour (ok ? theme::send.brighter (0.5f) : theme::mute.brighter (0.3f));
        g.setFont (font);
        g.drawText (label, pill, juce::Justification::centred);
    }

    drawPanel (g, sendArea, "SEND TO vMIX", "Each enabled row is its own NDI audio source");
    drawPanel (g, returnArea, "vMIX RETURN", "NDI feed played into the headphone mixes");
    drawPanel (g, computerArea, "COMPUTER AUDIO", "Virtual output for this computer's sound");
    drawPanel (g, matrixArea, "HEADPHONE MIXES", "Lit = heard on that output");

    // Send panel column headings
    if (! sendRows.isEmpty())
    {
        auto cols = SendRow::columns (sendHeadingRow);
        g.setColour (theme::dim);
        g.setFont (uiFont (11.5f, true));
        g.drawText ("INPUT", cols[SendRow::chan], juce::Justification::centredLeft);
        g.drawText ("NDI", cols[SendRow::on], juce::Justification::centred);
        g.drawText ("SOURCE NAME", cols[SendRow::name].withTrimmedLeft (4), juce::Justification::centredLeft);
        g.drawText ("LEVEL", cols[SendRow::gain].withTrimmedLeft (4), juce::Justification::centredLeft);
        g.drawText ("METER", cols[SendRow::meter].withTrimmedLeft (4), juce::Justification::centredLeft);
        g.drawText ("CHANNELS", cols[SendRow::stereo], juce::Justification::centred);
        g.drawText ("COUGH", cols[SendRow::mute], juce::Justification::centred);
        g.drawText ("STATUS", cols[SendRow::status].withTrimmedLeft (6), juce::Justification::centredLeft);
    }

    // Matrix footnote
    g.setColour (theme::dim);
    g.setFont (uiFont (12.0f));
    g.drawFittedText ("Shift-click a cell to set both sides. Turn the TASCAM's MONITOR MIX knob fully to COMPUTER "
                      "so the headphones only hear this matrix.",
                      matrixArea.reduced (14, 10).removeFromBottom (34), juce::Justification::bottomLeft, 2, 1.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    headerArea = area.removeFromTop (58);
    {
        auto h = headerArea;
        auto top = h.removeFromTop (32);
        interfaceButton.setBounds (top.removeFromRight (170).reduced (0, 2));
        interfaceSummary.setBounds (h.withTrimmedLeft (-4));
    }
    area.removeFromTop (8);

    auto left = area.removeFromLeft (juce::roundToInt (area.getWidth() * 0.6));
    left.removeFromRight (8);
    area.removeFromLeft (8);
    matrixArea = area;

    // Send panel (scrolls if the interface has lots of inputs)
    const int returnHeight = 128, computerHeight = 150, chrome = 34 + 24 + 12;
    const int rowsHeight = sendRows.size() * SendRow::rowHeight;
    const int maxSendHeight = left.getHeight() - returnHeight - computerHeight - 16;
    sendArea = left.removeFromTop (juce::jmax (chrome + 2 * SendRow::rowHeight, juce::jmin (chrome + rowsHeight, maxSendHeight)));
    {
        auto inner = sendArea.reduced (12, 0).withTrimmedTop (34);
        sendHeadingRow = inner.removeFromTop (24).withTrimmedBottom (4);
        inner.removeFromBottom (12);
        sendViewport.setBounds (inner);

        const bool scrolls = rowsHeight > inner.getHeight();
        const int width = inner.getWidth() - (scrolls ? sendViewport.getScrollBarThickness() + 2 : 0);
        sendRowsHolder.setSize (width, rowsHeight);

        for (int i = 0; i < sendRows.size(); ++i)
            sendRows[i]->setBounds (0, i * SendRow::rowHeight, width, SendRow::rowHeight);

        if (scrolls)
            sendHeadingRow.setWidth (width);
    }
    left.removeFromTop (8);

    // Return panel
    returnArea = left.removeFromTop (returnHeight);
    {
        auto inner = returnArea.reduced (14, 0).withTrimmedTop (36);
        auto line1 = inner.removeFromTop (30);
        returnCombo.setBounds (line1.removeFromLeft (juce::jmax (220, line1.getWidth() - 330)).reduced (0, 2));
        line1.removeFromLeft (10);
        returnGain.setBounds (line1.removeFromLeft (200));
        line1.removeFromLeft (8);
        returnMeter->setBounds (line1.reduced (0, 11));

        inner.removeFromTop (6);
        auto line2 = inner.removeFromTop (26);
        line2.removeFromLeft (56); // "Buffer" label
        returnBuffer.setBounds (line2.removeFromLeft (220));
        line2.removeFromLeft (12);
        returnStatus.setBounds (line2);
    }
    left.removeFromTop (8);

    // Computer audio panel
    computerArea = left.removeFromTop (juce::jmin (left.getHeight(), computerHeight));
    {
        auto inner = computerArea.reduced (14, 0).withTrimmedTop (36);
        auto line1 = inner.removeFromTop (30);
        computerCombo.setBounds (line1.removeFromLeft (juce::jmax (200, line1.getWidth() - 380)).reduced (0, 2));
        line1.removeFromLeft (8);
        computerRescan.setBounds (line1.removeFromLeft (80).reduced (0, 2));
        line1.removeFromLeft (64); // "Buffer" label
        computerBuffer.setBounds (line1.removeFromLeft (220));

        inner.removeFromTop (6);
        computerStatus.setBounds (inner.removeFromTop (22));
        computerHint.setBounds (inner.removeFromTop (40));
    }

    matrix->setBounds (matrixArea.reduced (8, 0).withTrimmedTop (36).withTrimmedBottom (46));
    repaint();
}

#include "SourceInspector.h"
#include "ndi/NdiLibrary.h"

namespace ui
{
//==============================================================================
class SourceInspector::Body : public juce::Component
{
public:
    Body (SourceInspector& o, int s, juce::Colour c, const std::vector<MixRef>& mixList)
        : owner (o), engine (o.engine), source (s), colour (c), mixes (mixList)
    {
        auto& cfg = engine.getSourceConfig (source);

        nameEditor.setFont (font (19.0f, true));
        nameEditor.setText (cfg.name, false);
        nameEditor.setIndents (4, 0);
        nameEditor.setTooltip (isReturn() ? juce::String ("Label for this source")
                                          : juce::String ("NDI source name (vMix shows it as COMPUTER (name))"));
        nameEditor.onReturnKey = [this] { commitName(); };
        nameEditor.onFocusLost = [this] { commitName(); };
        addAndMakeVisible (nameEditor);

        closeButton.setIcon (Icon::close);
        closeButton.setTooltip ("Close");
        closeButton.onClick = [this] { if (owner.onClose) owner.onClose(); };
        addAndMakeVisible (closeButton);

        if (! isReturn())
        {
            sendSwitch.setToggleState (cfg.sendEnabled, juce::dontSendNotification);
            sendSwitch.setTooltip ("Publish this as an NDI source for vMix");
            sendSwitch.onClick = [this] { engine.getSourceConfig (source).sendEnabled = sendSwitch.getToggleState(); applySource(); };
            addAndMakeVisible (sendSwitch);

            stereoButton.setToggleState (cfg.sendStereo, juce::dontSendNotification);
            stereoButton.setButtonText (cfg.sendStereo ? "Stereo send" : "Mono send");
            stereoButton.setTooltip ("Stereo: a 2-channel NDI source (mics go out dual-mono). Mono: 1 channel.");
            stereoButton.onClick = [this]
            {
                engine.getSourceConfig (source).sendStereo = stereoButton.getToggleState();
                stereoButton.setButtonText (stereoButton.getToggleState() ? "Stereo send" : "Mono send");
                applySource();
            };
            addAndMakeVisible (stereoButton);

            coughButton.setToggleState (cfg.sendMuted, juce::dontSendNotification);
            coughButton.setTooltip ("Silences this source to vMix. Headphone mixes are not affected.");
            coughButton.onClick = [this] { engine.getSourceConfig (source).sendMuted = coughButton.getToggleState(); applySource(); };
            addAndMakeVisible (coughButton);
        }

        if (isReturn() || isComputer())
        {
            deviceCombo.onChange = [this] { deviceChosen(); };
            addAndMakeVisible (deviceCombo);
            refreshDeviceList (true);

            bufferSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            bufferSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
            bufferSlider.setRange (10.0, isReturn() ? 300.0 : 200.0, 1.0);
            bufferSlider.setTextValueSuffix (" ms");
            bufferSlider.setValue (isReturn() ? engine.getReturnBufferMs() : engine.getComputerBufferMs(), juce::dontSendNotification);
            bufferSlider.setColour (juce::Slider::trackColourId, pal().dim);
            bufferSlider.setTooltip ("Jitter buffer before this plays in the headphones. Raise it if you hear dropouts.");
            bufferSlider.onValueChange = [this]
            {
                if (isReturn()) engine.setReturnBufferMs (bufferSlider.getValue());
                else            engine.setComputerBufferMs (bufferSlider.getValue());
                if (owner.onSourceChanged) owner.onSourceChanged();
            };
            addAndMakeVisible (bufferSlider);
        }

        trimSlider.setValue (cfg.gainDb, juce::dontSendNotification);
        trimSlider.setTrackColour (pal().text);
        trimSlider.setTooltip (isReturn() ? juce::String ("Level of the return. Double-click for 0 dB.")
                                          : juce::String ("Trim: applies to the NDI send and every mix. Double-click for 0 dB."));
        trimSlider.onValueChange = [this] { engine.getSourceConfig (source).gainDb = (float) trimSlider.getValue(); applySource(); };
        addAndMakeVisible (trimSlider);
        addAndMakeVisible (meter);

        addButton.setIcon (Icon::plus);
        addButton.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addButton);

        buildRouteRows();
    }

    ~Body() override
    {
        nameEditor.onFocusLost = nullptr;
        nameEditor.onReturnKey = nullptr;
    }

    bool isReturn() const   { return source == bridge::returnSource; }
    bool isComputer() const { return source == bridge::computerSource; }

    void buildRouteRows()
    {
        rows.clear();
        for (auto& m : mixes)
            if (RoutingModel::isRouted (engine, m, source))
                addAndMakeVisible (rows.add (new RouteRow (*this, m)));
        layout();
    }

    void tick (const Levels& levels)
    {
        meter.push (levels.source[(size_t) source]);

        juce::String text;
        juce::Colour c = pal().dim;
        if (isReturn())
        {
            auto& rx = engine.getReceiver();
            if (! ndi::lib().isLoaded())                 { text = "NDI runtime not installed"; c = pal().danger; }
            else if (engine.getReturnSource().isEmpty()) { text = "Pick the vMix NDI output to monitor"; }
            else if (rx.isReceiving())
            {
                text = "Receiving " + juce::String (rx.getSourceChannels()) + " ch";
                if (auto* f = engine.getReturnStats())
                    text << juce::String::fromUTF8 (" · buffer ") << juce::String (juce::roundToInt (f->getBufferedMs())) << juce::String::fromUTF8 (" ms · drift ")
                         << juce::String (juce::roundToInt (f->getCorrectionPpm())) << juce::String::fromUTF8 (" ppm · dropouts ") << f->getUnderruns();
                c = pal().green;
            }
            else { text = rx.isConnected() ? "Connected, waiting for audio" : "Looking for the source"; c = pal().warn; }

            if (engine.getFinder().getChangeCount() != lastFinderChange && ! deviceCombo.isPopupActive())
                refreshDeviceList (false);
        }
        else
        {
            auto& slot = engine.getSendHub().getSlot (source);
            const auto& cfg = engine.getSourceConfig (source);
            if (coughButton.getToggleState() != cfg.sendMuted) // changed from Companion
                coughButton.setToggleState (cfg.sendMuted, juce::dontSendNotification);
            if (! ndi::lib().isLoaded())                   { text = "NDI runtime not installed"; c = pal().danger; }
            else if (! cfg.sendEnabled)                    { text = "Not sent to vMix"; }
            else if (cfg.sendMuted)                        { text = "Muted to vMix (cough)"; c = pal().danger; }
            else if (! slot.live.load())                   { text = isComputer() ? engine.getComputerAudioSummary() : juce::String ("Starting"); c = pal().warn; }
            else
            {
                const int n = slot.connections.load();
                text = "Live as \"" + juce::SystemStats::getComputerName() + " (" + cfg.name + juce::String::fromUTF8 (")\" · ")
                       + juce::String (n) + (n == 1 ? " receiver" : " receivers");
                c = n > 0 ? pal().green : pal().warn;
            }
        }

        if (text != status || c != statusColour)
        {
            status = text;
            statusColour = c;
            repaint (statusArea);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto where = whereArea;
        g.setColour (colour);
        g.fillEllipse (where.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f));
        g.setColour (p.dim);
        g.setFont (font (11.5f, true));
        g.drawText (engine.getSourceDeviceLabel (source).toUpperCase(), where.withTrimmedLeft (8.0f).toNearestInt(),
                    juce::Justification::centredLeft, true);

        g.setFont (font (12.5f));
        g.setColour (statusColour);
        g.drawFittedText (status, statusArea, juce::Justification::topLeft, 2);

        g.setColour (p.dim);
        g.setFont (font (11.5f, true));
        for (auto& h : headings)
            g.drawText (h.text, h.area, juce::Justification::centredLeft, false);

        g.setFont (font (12.5f));
        for (auto& n : notes)
            g.drawFittedText (n.text, n.area, juce::Justification::topLeft, 3, 1.0f);

        if (! isReturn())
        {
            g.setColour (p.text);
            g.setFont (font (14.0f));
            g.drawText ("Send to vMix over NDI", sendLabelArea, juce::Justification::centredLeft, false);
        }

        g.setColour (p.border2);
        for (auto y : dividers)
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());
    }

    void resized() override { layout(); }

    void layout()
    {
        headings.clear();
        notes.clear();
        dividers.clear();
        const int w = getWidth();
        int y = 0;

        whereArea = juce::Rectangle<float> (0, (float) y, (float) w - 44.0f, 18.0f);
        closeButton.setBounds (w - 34, y, 34, 34);
        y += 20;
        nameEditor.setBounds (0, y, w - 44, 34);
        y += 46;

        if (! isReturn())
        {
            sendLabelArea = { 0, y, w - 60, 28 };
            sendSwitch.setBounds (w - 46, y, 46, 28);
            y += 32;
            statusArea = { 0, y, w, 34 };
            y += 36;
            stereoButton.setBounds (0, y, (w - 8) / 2, 32);
            coughButton.setBounds ((w + 8) / 2, y, (w - 8) / 2, 32);
            y += 44;
        }

        if (isReturn() || isComputer())
        {
            headings.push_back ({ isReturn() ? "NDI SOURCE" : "PLAYS FROM", { 0, y, w, 16 } });
            y += 20;
            deviceCombo.setBounds (0, y, w, 32);
            y += 38;
            if (isComputer())
            {
               #if JUCE_MAC
                notes.push_back ({ "Set System Settings > Sound > Output to \"NDI Audio Bridge\".", { 0, y, w, 34 } });
               #else
                notes.push_back ({ "Set Windows' playback device to \"CABLE Input\" and pick \"CABLE Output\" here.", { 0, y, w, 34 } });
               #endif
                y += 36;
            }
            else
            {
                statusArea = { 0, y, w, 34 };
                y += 36;
            }
            headings.push_back ({ "BUFFER", { 0, y + 6, 70, 16 } });
            bufferSlider.setBounds (64, y, w - 64, 28);
            y += 40;
        }

        dividers.push_back (y - 6);
        headings.push_back ({ isReturn() ? "LEVEL" : "TRIM", { 0, y + 6, 60, 16 } });
        trimSlider.setBounds (56, y, w - 56, 28);
        y += 32;
        meter.setBounds (64, y, w - 64 - 60, 5);
        y += 22;

        dividers.push_back (y - 6);
        headings.push_back ({ juce::String::fromUTF8 ("IN THESE MIXES  ·  ") + juce::String (rows.size()), { 0, y + 4, w, 16 } });
        y += 28;

        for (auto* r : rows)
        {
            r->setBounds (0, y, w, 58);
            y += 64;
        }

        addButton.setBounds (0, y, w, 36);
        y += 48;

        if (getHeight() != y)
            setSize (w, y);
    }

private:
    struct Text { juce::String text; juce::Rectangle<int> area; };

    class RouteRow : public juce::Component
    {
    public:
        RouteRow (Body& b, MixRef m) : body (b), mix (m)
        {
            auto& e = body.engine;
            for (auto* btn : { &leftButton, &rightButton })
            {
                btn->setOnColour (body.colour);
                btn->setFontHeight (11.0f);
                addAndMakeVisible (btn);
            }
            leftButton.setVisible (mix.isStereo());
            rightButton.setVisible (mix.isStereo());
            leftButton.setToggleState (mix.isStereo() && RoutingModel::sideOn (e, mix, body.source, 0), juce::dontSendNotification);
            rightButton.setToggleState (mix.isStereo() && RoutingModel::sideOn (e, mix, body.source, 1), juce::dontSendNotification);
            leftButton.onClick = [this] { RoutingModel::setSide (body.engine, mix, body.source, 0, leftButton.getToggleState()); body.routesChanged(); };
            rightButton.onClick = [this] { RoutingModel::setSide (body.engine, mix, body.source, 1, rightButton.getToggleState()); body.routesChanged(); };

            removeButton.setIcon (Icon::close);
            removeButton.setKind (FlatButton::Kind::ghost);
            removeButton.setTooltip ("Remove from this mix");
            removeButton.onClick = [this] { RoutingModel::setRouted (body.engine, mix, body.source, false); body.routesChanged(); };
            addAndMakeVisible (removeButton);

            level.setTrackColour (body.colour);
            level.setValue (RoutingModel::level (e, mix, body.source), juce::dontSendNotification);
            level.setTooltip ("Level in this mix. Double-click for 0 dB.");
            level.onValueChange = [this]
            {
                RoutingModel::setLevel (body.engine, mix, body.source, (float) level.getValue());
                if (body.owner.onRoutingChanged) body.owner.onRoutingChanged();
            };
            addAndMakeVisible (level);
        }

        void paint (juce::Graphics& g) override
        {
            auto top = getLocalBounds().removeFromTop (24);
            top.removeFromRight (mix.isStereo() ? 90 : 34);
            g.setColour (pal().text);
            g.setFont (font (14.0f, true));
            const auto name = RoutingModel::name (body.engine, mix);
            const int nameW = juce::jmin (top.getWidth() - 40, (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 8);
            g.drawText (name, top.removeFromLeft (nameW), juce::Justification::centredLeft, true);
            g.setColour (pal().dim);
            g.setFont (font (11.5f));
            g.drawText (RoutingModel::outputLabel (body.engine, mix), top, juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (24);
            removeButton.setBounds (top.removeFromRight (24).withSizeKeepingCentre (24, 22));
            top.removeFromRight (6);
            rightButton.setBounds (top.removeFromRight (24).withSizeKeepingCentre (24, 20));
            top.removeFromRight (4);
            leftButton.setBounds (top.removeFromRight (24).withSizeKeepingCentre (24, 20));
            r.removeFromTop (4);
            level.setBounds (r.removeFromTop (28));
        }

    private:
        Body& body;
        MixRef mix;
        FlatButton leftButton { "L", FlatButton::Kind::toggle }, rightButton { "R", FlatButton::Kind::toggle };
        FlatButton removeButton;
        LevelSlider level { false, -60.0, 12.0, true };
    };

    void routesChanged()
    {
        // Called from a row's own button: rebuild the rows after this click has finished.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Body> (this)]
        {
            if (safe != nullptr)
            {
                safe->buildRouteRows();
                safe->repaint();
            }
        });

        if (owner.onRoutingChanged)
            owner.onRoutingChanged();
    }

    void applySource()
    {
        engine.applySourceConfig (source);
        if (owner.onSourceChanged)
            owner.onSourceChanged();
    }

    void commitName()
    {
        auto newName = nameEditor.getText().trim();
        if (newName.isEmpty())
            newName = AudioEngine::defaultSourceName (source);

        // NDI source names must be unique on this machine.
        for (int s = 0; s < bridge::numSources; ++s)
            if (s != source && engine.getSourceConfig (s).name.equalsIgnoreCase (newName) && engine.sourceExists (s))
                newName << " 2";

        nameEditor.setText (newName, false);
        if (newName != engine.getSourceConfig (source).name)
        {
            engine.getSourceConfig (source).name = newName;
            applySource();
        }
    }

    void refreshDeviceList (bool force)
    {
        juce::StringArray items;
        juce::String current;

        if (isReturn())
        {
            lastFinderChange = engine.getFinder().getChangeCount();
            items = engine.getFinder().getSources();
            current = engine.getReturnSource();
        }
        else
        {
            items = engine.getComputerAudioDeviceNames();
            current = engine.getComputerAudioDeviceName();
        }

        if (current.isNotEmpty() && ! items.contains (current))
            items.insert (0, current);

        if (! force && items == shownItems)
            return;

        shownItems = items;
        deviceCombo.clear (juce::dontSendNotification);
        deviceCombo.addItem (isReturn() ? "(None)" : "(Off)", 1);
        for (int i = 0; i < items.size(); ++i)
            deviceCombo.addItem (items[i], i + 2);
        deviceCombo.setSelectedId (current.isEmpty() ? 1 : items.indexOf (current) + 2, juce::dontSendNotification);
    }

    void deviceChosen()
    {
        const auto id = deviceCombo.getSelectedId();
        const auto name = id <= 1 ? juce::String() : deviceCombo.getText();

        if (isReturn())
            engine.setReturnSource (name);
        else
            engine.openComputerAudioDevice (name);

        if (owner.onSourceChanged)
            owner.onSourceChanged();
    }

    void showAddMenu()
    {
        juce::PopupMenu menu;
        int added = 0;
        for (size_t i = 0; i < mixes.size(); ++i)
        {
            if (RoutingModel::isRouted (engine, mixes[i], source))
                continue;
            menu.addItem ((int) i + 1, RoutingModel::name (engine, mixes[i]) + "  (" + RoutingModel::subtitle (engine, mixes[i]) + ")");
            ++added;
        }

        if (added == 0)
            menu.addItem (-1, "Already in every mix", false);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                            [safe = juce::Component::SafePointer<Body> (this)] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;
                                RoutingModel::setRouted (safe->engine, safe->mixes[(size_t) result - 1], safe->source, true);
                                safe->routesChanged();
                            });
    }

    friend class RouteRow;
    SourceInspector& owner;
    AudioEngine& engine;
    const int source;
    const juce::Colour colour;
    std::vector<MixRef> mixes;

    juce::TextEditor nameEditor;
    FlatButton closeButton;
    Switch sendSwitch;
    FlatButton stereoButton { "Stereo send", FlatButton::Kind::toggle };
    FlatButton coughButton { "Cough mute", FlatButton::Kind::danger };
    juce::ComboBox deviceCombo;
    juce::Slider bufferSlider;
    LevelSlider trimSlider { false, -24.0, 24.0, true };
    LevelMeter meter;
    FlatButton addButton { "Add to a mix", FlatButton::Kind::ghost };
    juce::OwnedArray<RouteRow> rows;

    juce::Rectangle<float> whereArea;
    juce::Rectangle<int> statusArea, sendLabelArea;
    std::vector<Text> headings, notes;
    std::vector<int> dividers;
    juce::String status;
    juce::Colour statusColour;
    juce::StringArray shownItems;
    int lastFinderChange = -1;
};

//==============================================================================
SourceInspector::SourceInspector (AudioEngine& e) : engine (e)
{
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

SourceInspector::~SourceInspector()
{
    viewport.setViewedComponent (nullptr, false);
}

void SourceInspector::show (int s, juce::Colour colour, const std::vector<MixRef>& mixes)
{
    source = s;
    viewport.setViewedComponent (nullptr, false);
    body = std::make_unique<Body> (*this, s, colour, mixes);
    viewport.setViewedComponent (body.get(), false);
    resized();
}

void SourceInspector::refreshRoutes()
{
    if (body != nullptr)
        body->buildRouteRows();
}

void SourceInspector::tick (const Levels& levels)
{
    if (body != nullptr)
        body->tick (levels);
}

void SourceInspector::paint (juce::Graphics& g)
{
    g.fillAll (pal().panel);
    g.setColour (pal().border);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void SourceInspector::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedLeft (1).reduced (20, 18));
    if (body != nullptr)
    {
        body->setSize (viewport.getWidth() - (viewport.getVerticalScrollBar().isVisible() ? 10 : 0), body->getHeight());
        body->layout();
    }
}
} // namespace ui

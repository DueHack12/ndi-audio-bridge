#include "Pages.h"
#include "engine/Settings.h"
#include "ndi/NdiLibrary.h"

namespace ui
{
//==============================================================================
/** One input device or one output device. */
class InterfacesPage::Card : public juce::Component
{
public:
    Card (InterfacesPage& o, int s, bool isInput) : owner (o), engine (o.engine), slot (s), input (isInput)
    {
        for (auto* box : { &typeBox, &deviceBox, &rateBox, &bufferBox, &usedBox })
            addAndMakeVisible (box);

        typeBox.onChange = [this]
        {
            const auto type = typeBox.getText();
            const auto names = engine.getDeviceNames (type, input, true);
            const auto current = input ? engine.getInputDeviceName (slot) : engine.getOutputDeviceName (slot);
            juce::String pick = names.contains (current) ? current : juce::String();
            for (auto& n : names)
                if (pick.isEmpty() && ! inUse (type, n))
                    pick = n;
            switchTo (type, pick);
        };

        deviceBox.onChange = [this]
        {
            const int index = deviceBox.getSelectedId() - 1;
            if (juce::isPositiveAndBelow (index, deviceNames.size()))
                switchTo (engine.getInterfaceConfig (slot).typeName, deviceNames[index]);
        };

        rateBox.onChange = [this]
        {
            auto cfg = engine.getInterfaceConfig (slot);
            cfg.sampleRate = rateBox.getText().getDoubleValue();
            reopen (cfg);
        };
        bufferBox.onChange = [this]
        {
            auto cfg = engine.getInterfaceConfig (slot);
            cfg.bufferSize = bufferBox.getText().getIntValue();
            reopen (cfg);
        };
        usedBox.onChange = [this]
        {
            auto cfg = engine.getInterfaceConfig (slot);
            (input ? cfg.maxIns : cfg.maxOuts) = usedBox.getSelectedId() <= 1 ? 0 : usedBox.getSelectedId() - 1;
            reopen (cfg);
        };

        removeButton.setTooltip (input ? "Stop using this device's inputs" : "Stop using this device's outputs");
        removeButton.onClick = [this]
        {
            juce::MessageManager::callAsync ([&e = engine, s = slot, in = input, safe = juce::Component::SafePointer<InterfacesPage> (&owner)]
            {
                if (in) e.removeInput (s);
                else    e.removeOutput (s);
                if (safe != nullptr)
                    safe->changedLater();
            });
        };
        addAndMakeVisible (removeButton);

        populate();
    }

    int getSlot() const noexcept { return slot; }

    void populate()
    {
        const auto& cfg = engine.getInterfaceConfig (slot);
        const auto current = input ? cfg.inputName : cfg.outputName;

        typeBox.clear (juce::dontSendNotification);
        const auto types = engine.getDeviceTypeNames();
        for (int i = 0; i < types.size(); ++i)
            typeBox.addItem (types[i], i + 1);
        typeBox.setSelectedId (types.indexOf (cfg.typeName) + 1, juce::dontSendNotification);

        deviceNames = engine.getDeviceNames (cfg.typeName, input);
        deviceBox.clear (juce::dontSendNotification);
        for (int i = 0; i < deviceNames.size(); ++i)
        {
            const bool taken = deviceNames[i] != current && inUse (cfg.typeName, deviceNames[i]);
            deviceBox.addItem (deviceNames[i] + (taken ? juce::String (input ? "  (already an input)" : "  (already an output)") : juce::String()), i + 1);
            deviceBox.setItemEnabled (i + 1, ! taken);
        }
        if (deviceNames.contains (current))
            deviceBox.setSelectedId (deviceNames.indexOf (current) + 1, juce::dontSendNotification);
        else
            deviceBox.setText (current + " (not connected)", juce::dontSendNotification);

        auto fill = [] (juce::ComboBox& box, const juce::StringArray& items, const juce::String& selected)
        {
            box.clear (juce::dontSendNotification);
            for (int i = 0; i < items.size(); ++i)
                box.addItem (items[i], i + 1);
            if (items.contains (selected))
                box.setSelectedId (items.indexOf (selected) + 1, juce::dontSendNotification);
            else
                box.setText (selected, juce::dontSendNotification);
        };

        juce::StringArray rates, sizes;
        for (auto r : engine.getSampleRates (slot)) rates.add (juce::String (juce::roundToInt (r)));
        for (auto b : engine.getBufferSizes (slot)) sizes.add (juce::String (b));
        const auto rate = engine.isInterfaceRunning (slot) ? engine.getInterfaceSampleRate (slot) : cfg.sampleRate;
        fill (rateBox, rates, juce::String (juce::roundToInt (rate)));
        fill (bufferBox, sizes, juce::String (cfg.bufferSize));

        const int available = juce::jmin (input ? engine.getDeviceInputCount (slot) : engine.getDeviceOutputCount (slot),
                                           input ? bridge::maxInsPerInterface : bridge::maxPairsPerInterface * 2);
        const char* what = input ? "inputs" : "outputs";
        const int used = input ? cfg.maxIns : cfg.maxOuts;
        usedBox.clear (juce::dontSendNotification);
        usedBox.addItem ("All " + juce::String (available) + " " + what, 1);
        for (int i = 1; i <= available; ++i)
            usedBox.addItem (juce::String (i) + " " + what, i + 1);
        usedBox.setSelectedId (used > 0 && used <= available ? used + 1 : 1, juce::dontSendNotification);

        shared = input ? engine.hasOutput (slot) : engine.hasInput (slot);
        rateBox.setTooltip (shared ? "Shared with this device's " + juce::String (input ? "outputs" : "inputs") : juce::String());
        bufferBox.setTooltip (rateBox.getTooltip());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (p.panel);
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (p.border);
        g.drawRoundedRectangle (r, 14.0f, 1.0f);

        drawIcon (g, input ? Icon::mic : Icon::speaker, juce::Rectangle<float> (18.0f, 18.0f, 22.0f, 22.0f), p.dim);

        const int textW = removeButton.getX() - 60;
        g.setColour (p.text);
        g.setFont (font (15.5f, true));
        g.drawText (input ? engine.getInputDeviceName (slot) : engine.getOutputDeviceName (slot), 50, 12, textW, 22,
                    juce::Justification::centredLeft, true);

        const bool running = engine.isInterfaceRunning (slot);
        const auto status = engine.getInterfaceStatus (slot, input ? 1 : 2);
        g.setColour (running && status != "Not responding" ? p.green : p.danger);
        g.fillEllipse (50.0f, 40.0f, 8.0f, 8.0f);
        g.setColour (p.dim);
        g.setFont (font (12.0f));
        g.drawText (status, 64, 34, textW - 14, 20, juce::Justification::centredLeft, true);

        g.setFont (font (11.0f, true));
        for (auto& l : labels)
            g.drawText (l.first, l.second, juce::Justification::bottomLeft, false);

        if (shared)
        {
            g.setFont (font (11.5f));
            g.setColour (p.faint);
            g.drawText (juce::String::fromUTF8 ("Same device as an ") + (input ? "output" : "input")
                            + juce::String::fromUTF8 (": rate and buffer are shared, no extra latency between them"),
                        sharedNote, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        labels.clear();
        removeButton.setBounds (getWidth() - 96, 16, 80, 28);

        const int x = 18, w = getWidth() - 36, gap = 10;
        int y = 62;
        const int typeW = juce::jmax (110, (int) (w * 0.32f));
        labels.push_back ({ "DRIVER", { x, y, typeW, 16 } });
        typeBox.setBounds (x, y + 20, typeW, 30);
        labels.push_back ({ "DEVICE", { x + typeW + gap, y, w - typeW - gap, 16 } });
        deviceBox.setBounds (x + typeW + gap, y + 20, w - typeW - gap, 30);

        y += 60;
        const int cw = (w - 2 * gap) / 3;
        labels.push_back ({ "SAMPLE RATE", { x, y, cw, 16 } });
        rateBox.setBounds (x, y + 20, cw, 30);
        labels.push_back ({ "BUFFER", { x + cw + gap, y, cw, 16 } });
        bufferBox.setBounds (x + cw + gap, y + 20, cw, 30);
        labels.push_back ({ input ? "INPUTS USED" : "OUTPUTS USED", { x + 2 * (cw + gap), y, w - 2 * (cw + gap), 16 } });
        usedBox.setBounds (x + 2 * (cw + gap), y + 20, w - 2 * (cw + gap), 30);

        sharedNote = { x, y + 56, w, 18 };
    }

    static int preferredHeight (bool sharedDevice) { return sharedDevice ? 206 : 186; }
    bool isShared() const noexcept { return shared; }

private:
    bool inUse (const juce::String& type, const juce::String& name) const
    {
        for (int i = 0; i < bridge::maxInterfaces; ++i)
        {
            const auto& c = engine.getInterfaceConfig (i);
            if (engine.isInterfaceUsed (i) && c.typeName == type && (input ? c.inputName : c.outputName) == name)
                return true;
        }
        return false;
    }

    void switchTo (const juce::String& type, const juce::String& name)
    {
        if (name.isEmpty())
        {
            populate();
            return;
        }

        const int result = input ? engine.setInputDevice (slot, type, name) : engine.setOutputDevice (slot, type, name);
        if (result < 0)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "NDI Audio Bridge",
                                                    name + " is already used as an " + (input ? "input." : "output."));
        owner.changedLater();
    }

    void reopen (const bridge::InterfaceConfig& cfg)
    {
        engine.setInterfaceConfig (slot, cfg);
        engine.openInterface (slot);
        owner.changedLater();
    }

    InterfacesPage& owner;
    AudioEngine& engine;
    const int slot;
    const bool input;
    bool shared = false;
    juce::StringArray deviceNames;
    juce::ComboBox typeBox, deviceBox, rateBox, bufferBox, usedBox;
    FlatButton removeButton { "Remove", FlatButton::Kind::normal };
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;
    juce::Rectangle<int> sharedNote;
};

//==============================================================================
InterfacesPage::InterfacesPage (AudioEngine& e) : engine (e)
{
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    addInputButton.setIcon (Icon::plus);
    addInputButton.onClick = [this] { addDevice (true); };
    holder.addAndMakeVisible (addInputButton);

    addOutputButton.setIcon (Icon::plus);
    addOutputButton.onClick = [this] { addDevice (false); };
    holder.addAndMakeVisible (addOutputButton);

    rescanButton.setFontHeight (12.5f);
    rescanButton.onClick = [this]
    {
        for (auto& t : engine.getDeviceTypeNames())
            engine.getDeviceNames (t, true, true);
        engine.retryInterfaces();
        changedLater();
    };
    addAndMakeVisible (rescanButton);
}

InterfacesPage::~InterfacesPage()
{
    viewport.setViewedComponent (nullptr, false);
}

void InterfacesPage::changedLater()
{
    // Cards may be replaced (a device can move to another slot): never from inside their own callbacks.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<InterfacesPage> (this)]
    {
        if (safe == nullptr)
            return;
        safe->rebuild();
        if (safe->onChanged)
            safe->onChanged();
    });
}

void InterfacesPage::rebuild()
{
    inputCards.clear();
    outputCards.clear();
    for (int i = 0; i < bridge::maxInterfaces; ++i)
    {
        if (engine.hasInput (i))
            holder.addAndMakeVisible (inputCards.add (new Card (*this, i, true)));
        if (engine.hasOutput (i))
            holder.addAndMakeVisible (outputCards.add (new Card (*this, i, false)));
    }
    resized();
    repaint();
}

void InterfacesPage::tick()
{
    for (auto* c : inputCards)
        c->repaint (0, 30, c->getWidth(), 28);
    for (auto* c : outputCards)
        c->repaint (0, 30, c->getWidth(), 28);
}

void InterfacesPage::addDevice (bool input)
{
    const auto type = engine.getDefaultDeviceTypeName();
    const auto names = engine.getDeviceNames (type, input, true);

    juce::String pick;
    for (auto& n : names)
    {
        bool used = false;
        for (int i = 0; i < bridge::maxInterfaces; ++i)
        {
            const auto& c = engine.getInterfaceConfig (i);
            used = used || (engine.isInterfaceUsed (i) && c.typeName == type && (input ? c.inputName : c.outputName) == n);
        }
        if (! used) { pick = n; break; }
    }

    if (pick.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "NDI Audio Bridge",
                                                juce::String ("Every ") + (input ? "input" : "output") + " device is already in use.");
        return;
    }

    if ((input ? engine.addInput (type, pick) : engine.addOutput (type, pick)) < 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "NDI Audio Bridge",
                                                "Up to " + juce::String (bridge::maxInterfaces) + " devices can be used at once.");
        return;
    }

    changedLater();
}

void InterfacesPage::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.fillAll (p.bg);
    g.setColour (p.text);
    g.setFont (font (22.0f, true));
    g.drawText ("Inputs & outputs", 32, 24, 400, 30, juce::Justification::centredLeft, false);
    g.setColour (p.dim);
    g.setFont (font (13.5f));
    g.drawText ("Pick any device for each. An input and an output on the same device share a clock, so headphones hear it with no extra delay.",
                32, 56, rescanButton.getX() - 48, 20, juce::Justification::centredLeft, true);

    for (auto& h : headings)
    {
        g.setColour (p.text);
        g.setFont (font (12.0f, true));
        g.drawText (h.first, h.second.withHeight (18), juce::Justification::centredLeft, false);
    }
    g.setColour (p.dim);
    g.setFont (font (12.5f));
    for (auto& n : notes)
        g.drawText (n.first, n.second, juce::Justification::centredLeft, true);
}

void InterfacesPage::resized()
{
    rescanButton.setBounds (getWidth() - 32 - 140, 26, 140, 30);
    viewport.setBounds (getLocalBounds().withTrimmedTop (140).reduced (32, 0));

    const int w = viewport.getWidth() - 12, gap = 24;
    const int colW = (w - gap) / 2;

    headings.clear();
    notes.clear();
    headings.push_back ({ "INPUTS", { 32, 96, colW, 18 } });
    notes.push_back ({ "Each input channel goes to vMix as its own NDI source", { 32, 114, colW, 18 } });
    headings.push_back ({ "OUTPUTS", { 32 + colW + gap, 96, colW, 18 } });
    notes.push_back ({ "Headphone mixes and speakers", { 32 + colW + gap, 114, colW, 18 } });

    auto column = [&] (int x, juce::OwnedArray<Card>& cards, FlatButton& add)
    {
        int y = 0;
        for (auto* c : cards)
        {
            const int h = Card::preferredHeight (c->isShared());
            c->setBounds (x, y, colW, h);
            y += h + 14;
        }
        add.setBounds (x, y, colW, 40);
        return y + 60;
    };

    const int left = column (0, inputCards, addInputButton);
    const int right = column (colW + gap, outputCards, addOutputButton);
    holder.setSize (w, juce::jmax (left, right));
    holder.repaint();
}

//==============================================================================
SettingsPage::SettingsPage (AudioEngine& e) : engine (e)
{
    lightButton.setRadioGroupId (201);
    darkButton.setRadioGroupId (201);
    lightButton.onClick = [this] { if (lightButton.getToggleState() && onThemeChosen) onThemeChosen (false); };
    darkButton.onClick = [this] { if (darkButton.getToggleState() && onThemeChosen) onThemeChosen (true); };
    addAndMakeVisible (lightButton);
    addAndMakeVisible (darkButton);

    makeMsSlider (returnSlider, 10, 300, [this] (double v) { engine.setReturnBufferMs (v); });
    makeMsSlider (computerSlider, 10, 200, [this] (double v) { engine.setComputerBufferMs (v); });
    makeMsSlider (crossSlider, 5, 100, [this] (double v) { engine.setCrossBufferMs (v); });
    addAndMakeVisible (githubLink);
    refresh();
}

juce::Slider& SettingsPage::makeMsSlider (juce::Slider& s, double min, double max, std::function<void (double)> set)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    s.setRange (min, max, 1.0);
    s.setTextValueSuffix (" ms");
    s.onValueChange = [this, &s, set] { set (s.getValue()); if (onChanged) onChanged(); };
    addAndMakeVisible (s);
    return s;
}

void SettingsPage::refresh()
{
    lightButton.setToggleState (! isDark(), juce::dontSendNotification);
    darkButton.setToggleState (isDark(), juce::dontSendNotification);
    returnSlider.setValue (engine.getReturnBufferMs(), juce::dontSendNotification);
    computerSlider.setValue (engine.getComputerBufferMs(), juce::dontSendNotification);
    crossSlider.setValue (engine.getCrossBufferMs(), juce::dontSendNotification);
    for (auto* s : { &returnSlider, &computerSlider, &crossSlider })
        s->setColour (juce::Slider::trackColourId, pal().accent);
    repaint();
}

void SettingsPage::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.fillAll (p.bg);
    g.setColour (p.text);
    g.setFont (font (22.0f, true));
    g.drawText ("Settings", 32, 24, 400, 30, juce::Justification::centredLeft, false);

    g.setColour (p.dim);
    g.setFont (font (11.5f, true));
    for (auto& h : headings)
        g.drawText (h.first, h.second, juce::Justification::bottomLeft, false);

    g.setColour (p.text);
    g.setFont (font (14.0f));
    for (auto& l : labels)
        g.drawText (l.first, l.second, juce::Justification::centredLeft, false);

    g.setColour (p.dim);
    g.setFont (font (12.5f));
    for (auto& n : notes)
        g.drawFittedText (n.first, n.second, juce::Justification::topLeft, 3);
}

void SettingsPage::resized()
{
    headings.clear();
    labels.clear();
    notes.clear();

    const int x = 32, w = juce::jmin (640, getWidth() - 64);
    int y = 80;

    headings.push_back ({ "APPEARANCE", { x, y, w, 16 } });
    y += 24;
    lightButton.setBounds (x, y, 110, 34);
    darkButton.setBounds (x + 118, y, 110, 34);
    y += 58;

    headings.push_back ({ "BUFFERS (LOWER = LESS DELAY, HIGHER = FEWER DROPOUTS)", { x, y, w, 16 } });
    y += 26;
    auto sliderRow = [&] (const juce::String& label, juce::Slider& s, const juce::String& note)
    {
        labels.push_back ({ label, { x, y, 220, 28 } });
        s.setBounds (x + 230, y, w - 230, 28);
        y += 30;
        notes.push_back ({ note, { x, y, w, 18 } });
        y += 30;
    };
    sliderRow ("vMix return", returnSlider, "Network jitter buffer for the NDI return.");
    sliderRow ("Computer audio", computerSlider, "Buffer for computer audio heard in the headphones.");
    sliderRow ("Between devices", crossSlider, "Used only when a mix plays an input from a different device.");
    y += 10;

    headings.push_back ({ "NDI", { x, y, w, 16 } });
    y += 24;
    notes.push_back ({ ndi::lib().isLoaded() ? "Runtime " + ndi::lib().getStatusText() + "\n" + ndi::lib().getLoadedPath()
                                             : ndi::lib().getStatusText(),
                       { x, y, w, 40 } });
    y += 56;

    headings.push_back ({ "ABOUT", { x, y, w, 16 } });
    y += 24;
    notes.push_back ({ juce::String ("NDI Audio Bridge ") + JUCE_APPLICATION_VERSION_STRING + "\nSettings: "
                           + Settings::getFile().getFile().getFullPathName(),
                       { x, y, w, 40 } });
    y += 44;
    githubLink.setBounds (x - 4, y, 320, 24);
    githubLink.setJustificationType (juce::Justification::centredLeft);
}
} // namespace ui

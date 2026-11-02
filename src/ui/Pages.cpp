#include "Pages.h"
#include "engine/Settings.h"
#include "ndi/NdiLibrary.h"

namespace ui
{
//==============================================================================
class InterfacesPage::Card : public juce::Component
{
public:
    Card (InterfacesPage& o, int s) : owner (o), engine (o.engine), slot (s)
    {
        for (auto* box : { &typeBox, &inBox, &outBox, &rateBox, &bufferBox, &insUsedBox, &outsUsedBox })
            addAndMakeVisible (box);

        insUsedBox.onChange = [this]
        {
            auto cfg = engine.getInterfaceConfig (slot);
            cfg.maxIns = insUsedBox.getSelectedId() <= 1 ? 0 : insUsedBox.getSelectedId() - 1;
            apply (cfg);
        };
        outsUsedBox.onChange = [this]
        {
            auto cfg = engine.getInterfaceConfig (slot);
            cfg.maxOuts = outsUsedBox.getSelectedId() <= 1 ? 0 : (outsUsedBox.getSelectedId() - 1);
            apply (cfg);
        };

        typeBox.onChange = [this]
        {
            auto cfg = engine.getInterfaceConfig (slot);
            cfg.typeName = typeBox.getText();
            const auto outs = engine.getDeviceNames (cfg.typeName, false, true);
            const auto ins = engine.getDeviceNames (cfg.typeName, true);
            if (! outs.contains (cfg.outputName)) cfg.outputName = outs[0];
            if (! ins.contains (cfg.inputName))   cfg.inputName = ins.contains (cfg.outputName) ? cfg.outputName : ins[0];
            apply (cfg);
        };
        inBox.onChange = [this]
        {
            auto cfg = engine.getInterfaceConfig (slot);
            cfg.inputName = inBox.getSelectedId() <= 1 ? juce::String() : inBox.getText();
            apply (cfg);
        };
        outBox.onChange = [this]
        {
            auto cfg = engine.getInterfaceConfig (slot);
            cfg.outputName = outBox.getSelectedId() <= 1 ? juce::String() : outBox.getText();
            apply (cfg);
        };
        rateBox.onChange = [this]
        {
            auto cfg = engine.getInterfaceConfig (slot);
            cfg.sampleRate = rateBox.getText().getDoubleValue();
            apply (cfg);
        };
        bufferBox.onChange = [this]
        {
            auto cfg = engine.getInterfaceConfig (slot);
            cfg.bufferSize = bufferBox.getText().getIntValue();
            apply (cfg);
        };

        removeButton.onClick = [this]
        {
            juce::MessageManager::callAsync ([&e = engine, slotIndex = slot, safeOwner = juce::Component::SafePointer<InterfacesPage> (&owner)]
            {
                e.removeInterface (slotIndex);
                if (safeOwner != nullptr && safeOwner->onChanged)
                    safeOwner->onChanged();
            });
        };
        addAndMakeVisible (removeButton);

        populate();
    }

    void populate()
    {
        const auto& cfg = engine.getInterfaceConfig (slot);

        auto fill = [] (juce::ComboBox& box, const juce::StringArray& items, const juce::String& current, const juce::String& none)
        {
            box.clear (juce::dontSendNotification);
            int id = 1;
            if (none.isNotEmpty())
                box.addItem (none, id++);
            for (auto& i : items)
                box.addItem (i, id++);
            const int index = items.indexOf (current);
            box.setSelectedId (index >= 0 ? index + (none.isNotEmpty() ? 2 : 1) : (none.isNotEmpty() ? 1 : 0), juce::dontSendNotification);
            if (index < 0 && current.isNotEmpty())
                box.setText (current + " (not connected)", juce::dontSendNotification);
        };

        fill (typeBox, engine.getDeviceTypeNames(), cfg.typeName, {});
        fill (inBox, engine.getDeviceNames (cfg.typeName, true), cfg.inputName, "(no inputs)");
        fill (outBox, engine.getDeviceNames (cfg.typeName, false), cfg.outputName, "(no outputs)");

        juce::StringArray rates, sizes;
        for (auto r : engine.getSampleRates (slot)) rates.add (juce::String (juce::roundToInt (r)));
        for (auto b : engine.getBufferSizes (slot)) sizes.add (juce::String (b));
        const auto currentRate = engine.isInterfaceRunning (slot) ? engine.getInterfaceSampleRate (slot) : cfg.sampleRate;
        fill (rateBox, rates, juce::String (juce::roundToInt (currentRate)), {});
        fill (bufferBox, sizes, juce::String (cfg.bufferSize), {});

        auto fillCount = [] (juce::ComboBox& box, int available, int cap, int current, const char* what)
        {
            box.clear (juce::dontSendNotification);
            const int n = juce::jmin (available, cap);
            box.addItem ("All " + juce::String (n) + " " + what, 1);
            for (int i = 1; i <= n; ++i)
                box.addItem (juce::String (i) + " " + what, i + 1);
            box.setSelectedId (current > 0 && current <= n ? current + 1 : 1, juce::dontSendNotification);
        };
        fillCount (insUsedBox, engine.getDeviceInputCount (slot), bridge::maxInsPerInterface, cfg.maxIns, "inputs");
        fillCount (outsUsedBox, engine.getDeviceOutputCount (slot), bridge::maxPairsPerInterface * 2, cfg.maxOuts, "outputs");
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

        drawIcon (g, Icon::device, juce::Rectangle<float> (18.0f, 30.0f, 22.0f, 22.0f), p.dim);
        g.setColour (p.text);
        g.setFont (font (16.0f, true));
        g.drawText (engine.getInterfaceName (slot), 50, 14, getWidth() - 200, 22, juce::Justification::centredLeft, true);

        const bool running = engine.isInterfaceRunning (slot);
        const auto status = engine.getInterfaceStatus (slot);
        g.setColour (running && status != "Not responding" ? p.green : p.danger);
        g.fillEllipse (50.0f, 44.0f, 8.0f, 8.0f);
        g.setColour (p.dim);
        g.setFont (font (12.5f));
        g.drawText (status, 64, 38, getWidth() - 200, 20, juce::Justification::centredLeft, true);

        g.setFont (font (11.0f, true));
        for (auto& l : labels)
            g.drawText (l.first, l.second, juce::Justification::bottomLeft, false);
    }

    void resized() override
    {
        labels.clear();
        removeButton.setBounds (getWidth() - 110, 18, 92, 30);

        auto row = getLocalBounds().reduced (18, 0).withTrimmedTop (68).withHeight (52);
        const int gap = 10;
        const int w = row.getWidth();
        const int cols[] = { (int) (w * 0.16f), (int) (w * 0.30f), (int) (w * 0.30f), (int) (w * 0.12f) };
        juce::ComboBox* boxes[] = { &typeBox, &inBox, &outBox, &rateBox, &bufferBox };
        const char* names[] = { "DRIVER", "INPUTS FROM", "OUTPUTS TO", "SAMPLE RATE", "BUFFER" };

        int x = row.getX();
        for (int i = 0; i < 5; ++i)
        {
            const int cw = i < 4 ? cols[i] - gap : row.getRight() - x;
            labels.push_back ({ names[i], { x, row.getY(), cw, 16 } });
            boxes[i]->setBounds (x, row.getY() + 20, cw, 30);
            x += cw + gap;
        }

        auto row2 = row.translated (0, 62);
        labels.push_back ({ "INPUTS USED", { row2.getX(), row2.getY(), 200, 16 } });
        insUsedBox.setBounds (row2.getX(), row2.getY() + 20, 200, 30);
        labels.push_back ({ "OUTPUTS USED", { row2.getX() + 210, row2.getY(), 200, 16 } });
        outsUsedBox.setBounds (row2.getX() + 210, row2.getY() + 20, 200, 30);
    }

private:
    void apply (bridge::InterfaceConfig cfg)
    {
        engine.setInterfaceConfig (slot, cfg);
        engine.openInterface (slot);
        populate();
        if (owner.onChanged)
            owner.onChanged();
    }

    InterfacesPage& owner;
    AudioEngine& engine;
    const int slot;
    juce::ComboBox typeBox, inBox, outBox, rateBox, bufferBox, insUsedBox, outsUsedBox;
    FlatButton removeButton { "Remove", FlatButton::Kind::normal };
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;
};

//==============================================================================
InterfacesPage::InterfacesPage (AudioEngine& e) : engine (e)
{
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    addButton.setIcon (Icon::plus);
    addButton.onClick = [this] { addInterface(); };
    holder.addAndMakeVisible (addButton);

    rescanButton.onClick = [this]
    {
        for (auto& t : engine.getDeviceTypeNames())
            engine.getDeviceNames (t, true, true);
        engine.retryInterfaces();
        for (auto* c : cards)
            c->populate();
        if (onChanged) onChanged();
    };
    holder.addAndMakeVisible (rescanButton);
}

InterfacesPage::~InterfacesPage()
{
    viewport.setViewedComponent (nullptr, false);
}

void InterfacesPage::rebuild()
{
    cards.clear();
    for (int i = 0; i < bridge::maxInterfaces; ++i)
        if (engine.isInterfaceUsed (i))
            holder.addAndMakeVisible (cards.add (new Card (*this, i)));
    resized();
    repaint();
}

void InterfacesPage::tick()
{
    for (auto* c : cards)
        c->repaint (0, 30, c->getWidth(), 30);
}

void InterfacesPage::addInterface()
{
    juce::StringArray used;
    for (int i = 0; i < bridge::maxInterfaces; ++i)
        if (engine.isInterfaceUsed (i))
            used.add (engine.getInterfaceConfig (i).getKey());

    bridge::InterfaceConfig cfg;
    cfg.typeName = engine.getDefaultDeviceTypeName();
    const auto outs = engine.getDeviceNames (cfg.typeName, false, true);
    const auto ins = engine.getDeviceNames (cfg.typeName, true);

    for (auto& o : outs)
        if (! used.contains (o)) { cfg.outputName = o; break; }
    if (ins.contains (cfg.outputName))
        cfg.inputName = cfg.outputName;
    else
        for (auto& i : ins)
            if (! used.contains (i)) { cfg.inputName = i; break; }

    if (engine.addInterface (cfg) < 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "NDI Audio Bridge",
                                                "Up to " + juce::String (bridge::maxInterfaces) + " interfaces can be used at once.");
        return;
    }

    if (onChanged)
        onChanged();
}

void InterfacesPage::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.fillAll (p.bg);
    g.setColour (p.text);
    g.setFont (font (22.0f, true));
    g.drawText ("Audio interfaces", 32, 24, 400, 30, juce::Justification::centredLeft, false);
    g.setColour (p.dim);
    g.setFont (font (13.5f));
    g.drawText ("Each interface runs on its own clock. Its inputs go straight out over NDI; mixes can use inputs from any interface.",
                32, 56, getWidth() - 64, 20, juce::Justification::centredLeft, true);
}

void InterfacesPage::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (90).reduced (32, 0));
    const int w = viewport.getWidth() - 12;
    int y = 0;
    for (auto* c : cards)
    {
        c->setBounds (0, y, w, 200);
        y += 216;
    }
    addButton.setBounds (0, y, 220, 40);
    rescanButton.setBounds (232, y, 150, 40);
    addButton.setEnabled (cards.size() < bridge::maxInterfaces);
    y += 60;
    holder.setSize (w, y);
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
    sliderRow ("Between interfaces", crossSlider, "Used only when a mix plays an input from a different interface.");
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

#include "CompanionPage.h"
#include "engine/RemoteControl.h"
#include "engine/SystemMonitor.h"

namespace ui
{
namespace
{
struct Address
{
    juce::String ip, adapter;
    bool operator== (const Address& o) const { return ip == o.ip && adapter == o.adapter; }
    bool operator!= (const Address& o) const { return ! operator== (o); }
};

/** Real IPv4 addresses, the likely LAN one first (see SystemMonitor::listAddresses). */
std::vector<Address> machineAddresses()
{
    std::vector<Address> out;
    for (auto& a : SystemMonitor::listAddresses())
    {
        Address x { juce::String::fromUTF8 (a.ip.c_str()), juce::String::fromUTF8 (a.adapter.c_str()) };
        if (std::find_if (out.begin(), out.end(), [&] (const Address& y) { return y.ip == x.ip; }) == out.end())
            out.push_back (x);
    }
    return out;
}

juce::String agoText (juce::uint32 ms)
{
    const auto secs = (int) ((juce::Time::getMillisecondCounter() - ms) / 1000);
    if (secs < 2)    return "just now";
    if (secs < 120)  return juce::String (secs) + " s ago";
    if (secs < 7200) return juce::String (secs / 60) + " min ago";
    return juce::String (secs / 3600) + " h ago";
}

/** Text with *bold* spans, word-wrapped. */
juce::AttributedString rich (const juce::String& text, juce::Colour normal, juce::Colour strong)
{
    juce::AttributedString a;
    a.setWordWrap (juce::AttributedString::byWord);
    a.setLineSpacing (4.0f);

    bool bold = false;
    juce::String run;
    auto flush = [&]
    {
        if (run.isNotEmpty())
            a.append (run, font (13.0f, bold), bold ? strong : normal);
        run.clear();
    };

    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        if (*p == '*')
        {
            flush();
            bold = ! bold;
        }
        else
        {
            run += juce::String::charToString (*p);
        }
    }
    flush();
    return a;
}

void drawChip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour bg, juce::Colour fg)
{
    g.setColour (bg);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (fg);
    g.setFont (font (11.0f, true));
    g.drawText (text, r, juce::Justification::centred, false);
}
} // namespace

//==============================================================================
class CompanionPage::Content : public juce::Component
{
public:
    Content (CompanionPage& o) : owner (o), engine (o.engine), remote (o.remote)
    {
        enableSwitch.setTooltip ("Listen for OSC commands from Companion");
        enableSwitch.onClick = [this]
        {
            auto c = remote.getConfig();
            c.enabled = enableSwitch.getToggleState();
            remote.setConfig (c);
            changed();
        };
        addAndMakeVisible (enableSwitch);

        for (auto* e : { &portEditor, &feedbackEditor })
        {
            e->setFont (font (14.0f));
            e->setIndents (8, 0);
            e->setJustification (juce::Justification::centredLeft);
            addAndMakeVisible (e);
        }

        portEditor.setInputRestrictions (5, "0123456789");
        portEditor.onReturnKey = [this] { applyPort(); };
        portEditor.onFocusLost = [this] { applyPort(); };

        feedbackEditor.setTextToShowWhenEmpty ("Learned from the first command", pal().faint);
        feedbackEditor.onReturnKey = [this] { applyFeedback(); };
        feedbackEditor.onFocusLost = [this] { applyFeedback(); };

        refresh();
    }

    ~Content() override
    {
        for (auto* e : { &portEditor, &feedbackEditor })
        {
            e->onReturnKey = nullptr;
            e->onFocusLost = nullptr;
        }
    }

    void refresh()
    {
        const auto& c = remote.getConfig();
        enableSwitch.setToggleState (c.enabled, juce::dontSendNotification);
        if (! portEditor.hasKeyboardFocus (false))
            portEditor.setText (juce::String (c.port), false);
        if (! feedbackEditor.hasKeyboardFocus (false))
            feedbackEditor.setText (c.feedbackTo, false);
        feedbackEditor.setTextToShowWhenEmpty ("Learned from the first command", pal().faint);
        addresses = machineAddresses();
        layout();
        repaint();
    }

    void rebuild()
    {
        rows.clear();
        int order = 0;
        for (auto s : RoutingModel::listSources (engine))
        {
            const int index = order++;
            if (RemoteControl::addressFor (s).isEmpty())
                continue;
            addAndMakeVisible (rows.add (new Row (*this, s, sourceColour (index))));
        }
        layout();
        repaint();
    }

    void tick()
    {
        for (auto* r : rows)
            r->update();

        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastSlowTick < 1000)
            return;
        lastSlowTick = now;

        juce::String last = remote.getLastCommandTime() == 0
                                ? juce::String ("Nothing received yet")
                                : remote.getLastCommand() + "  from " + remote.getLastCommandFrom() + juce::String::fromUTF8 ("  \xc2\xb7  ")
                                      + agoText (remote.getLastCommandTime());
        const auto targets = remote.getFeedbackTargets();
        juce::String fb = targets.isEmpty()
                              ? juce::String ("Nowhere yet: press a button in Companion once (with Listen for Feedback on)")
                              : "To " + targets + (remote.getLastFeedbackTime() == 0 ? juce::String()
                                                                                    : juce::String::fromUTF8 ("  \xc2\xb7  last sent ")
                                                                                          + agoText (remote.getLastFeedbackTime()));
        const auto ips = machineAddresses();
        const auto& c = remote.getConfig();
        const bool configMoved = (! feedbackEditor.hasKeyboardFocus (false) && feedbackEditor.getText() != c.feedbackTo);

        if (last != lastCommandText || fb != feedbackText || ips != addresses || configMoved)
        {
            lastCommandText = last;
            feedbackText = fb;
            addresses = ips;
            if (configMoved)
                feedbackEditor.setText (c.feedbackTo, false);
            layout();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();

        g.setColour (p.dim);
        g.setFont (font (11.5f, true));
        for (auto& h : headings)
            g.drawText (h.first, h.second, juce::Justification::bottomLeft, false);

        for (auto& c : cards)
        {
            g.setColour (p.panel);
            g.fillRoundedRectangle (c.toFloat(), 12.0f);
            g.setColour (p.border);
            g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 12.0f, 1.0f);
        }

        g.setColour (p.border2);
        for (auto& d : dividers)
            g.drawHorizontalLine (d.getY(), (float) d.getX(), (float) d.getRight());

        g.setFont (font (14.0f));
        g.setColour (p.text);
        for (auto& l : labels)
            g.drawText (l.first, l.second, juce::Justification::centredLeft, true);

        g.setFont (font (12.5f));
        g.setColour (p.dim);
        for (auto& n : notes)
            g.drawFittedText (n.first, n.second, juce::Justification::centredLeft, 2, 1.0f);

        // this computer's address(es): the likely LAN one large, any others small underneath
        {
            auto area = computerArea.toFloat();
            if (addresses.empty())
            {
                g.setFont (font (13.0f));
                g.setColour (p.dim);
                g.drawText ("No network connection", area, juce::Justification::centredLeft, true);
            }
            else
            {
                auto line = addresses.size() > 1 ? area.removeFromTop (area.getHeight() * 0.56f) : area;
                const auto& first = addresses.front();
                g.setFont (monoFont (14.0f));
                g.setColour (p.text);
                const float ipW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), first.ip) + 12.0f;
                g.drawText (first.ip, line.removeFromLeft (ipW), addresses.size() > 1 ? juce::Justification::bottomLeft
                                                                                    : juce::Justification::centredLeft, false);
                g.setFont (font (12.5f));
                g.setColour (p.dim);
                g.drawText (first.adapter, line, addresses.size() > 1 ? juce::Justification::bottomLeft
                                                                       : juce::Justification::centredLeft, true);

                if (addresses.size() > 1)
                {
                    juce::StringArray others;
                    for (size_t i = 1; i < addresses.size(); ++i)
                        others.add (addresses[i].ip + (addresses[i].adapter.isNotEmpty() ? " (" + addresses[i].adapter + ")" : juce::String()));
                    g.setFont (font (11.5f));
                    g.setColour (p.faint);
                    g.drawText ("Also " + others.joinIntoString (", "), area.withTrimmedTop (2.0f), juce::Justification::topLeft, true);
                }
            }
        }

        for (auto& s : steps)
        {
            const auto circle = juce::Rectangle<float> ((float) s.area.getX(), (float) s.area.getY(), 22.0f, 22.0f);
            g.setColour (p.accent);
            g.fillEllipse (circle);
            g.setColour (juce::Colours::white);
            g.setFont (font (12.0f, true));
            g.drawText (juce::String (s.number), circle, juce::Justification::centred, false);
            s.layout.draw (g, s.area.withTrimmedLeft (34).withTrimmedTop (2).toFloat());
        }
    }

    void resized() override { layout(); }

    void layout()
    {
        headings.clear();
        labels.clear();
        notes.clear();
        cards.clear();
        dividers.clear();
        steps.clear();

        const int w = getWidth();
        if (w <= 0)
            return;

        const auto& c = remote.getConfig();
        const auto& p = pal();
        const int labelW = 190, controlX = 20 + labelW;
        int y = 0;

        // ---- connection
        headings.push_back ({ "CONNECTION", { 0, y, w, 16 } });
        y += 24;
        const int rowH = 46;
        const int cardTop = y;
        auto row = [&] (const juce::String& label)
        {
            if (y != cardTop)
                dividers.push_back ({ 16, y, w - 32, 1 });
            labels.push_back ({ label, { 20, y, labelW, rowH } });
            y += rowH;
            return y - rowH;
        };

        int r = row ("Companion control");
        enableSwitch.setBounds (controlX, r + 9, 46, 28);
        notes.push_back ({ "Turn off to ignore all OSC commands", { controlX + 62, r, w - controlX - 80, rowH } });

        r = row ("Listen on UDP port");
        portEditor.setBounds (controlX, r + 7, 96, 32);
        notes.push_back ({ juce::String::fromUTF8 ("Companion\xe2\x80\x99s \xe2\x80\x9cTarget Port\xe2\x80\x9d"), { controlX + 112, r, w - controlX - 130, rowH } });

        r = row ("Send feedback to");
        feedbackEditor.setBounds (controlX, r + 7, 210, 32);
        notes.push_back ({ juce::String::fromUTF8 ("Companion\xe2\x80\x99s IP : \xe2\x80\x9cSource Port\xe2\x80\x9d. Several? Separate with commas."),
                           { controlX + 226, r, w - controlX - 244, rowH } });

        r = row ("This computer");
        computerArea = { controlX, r + 6, w - controlX - 20, rowH - 12 };

        r = row ("Last command");
        notes.push_back ({ lastCommandText.isEmpty() ? juce::String ("Nothing received yet") : lastCommandText,
                           { controlX, r, w - controlX - 20, rowH } });

        r = row ("Feedback");
        notes.push_back ({ feedbackText, { controlX, r, w - controlX - 20, rowH } });

        cards.push_back ({ 0, cardTop, w, y - cardTop });
        y += 30;

        // ---- steps
        headings.push_back ({ "SET UP IN COMPANION", { 0, y, w, 16 } });
        y += 26;

        const auto ip = addresses.empty() ? juce::String ("this computer's IP") : addresses.front().ip;
        const auto port = juce::String (c.port);
        const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
        const juce::String stepText[] = {
            "Add a connection: *Generic: OSC*. Target Hostname or IP *" + ip + "*" + dot + "Target Port *" + port + "*" + dot
                + "Protocol *UDP*" + dot + "tick *Listen for Feedback*" + dot + "Source Port: any free port, e.g. *" + juce::String (c.port < 65535 ? c.port + 1 : c.port - 1) + "*.",
            "On a button, add the press action *Send message without arguments* with a mic's *toggle* path from the list below. "
            "For press-to-cough, send the *cough/on* path on press and *cough/off* on release.",
            "Add the feedback *Listen for OSC messages (Integer)* with the mic's *live* path, Value *1*, and a red background. "
            "*live* is 1 only while the mic is really going to vMix: not coughed, and vMix has this NDI source as an input "
            "(status *LIVE* below). Testing without vMix? Use the *cough* path instead: it lights while the mic is coughed."
        };

        for (int i = 0; i < 3; ++i)
        {
            Step s;
            s.number = i + 1;
            s.layout.createLayout (rich (stepText[i], p.dim, p.text), (float) (w - 34));
            const int h = juce::jmax (24, (int) std::ceil (s.layout.getHeight()) + 4);
            s.area = { 0, y, w, h };
            steps.push_back (std::move (s));
            y += h + 14;
        }
        y += 16;

        // ---- mics
        headings.push_back ({ "MICS", { 0, y, w, 16 } });
        y += 24;
        const int micTop = y;
        for (int i = 0; i < rows.size(); ++i)
        {
            if (i > 0)
                dividers.push_back ({ 16, y, w - 32, 1 });
            rows[i]->setBounds (0, y, w, 52);
            y += 52;
        }
        if (! rows.isEmpty())
            cards.push_back ({ 0, micTop, w, y - micTop });
        y += 16;

        notes.push_back ({ juce::String::fromUTF8 ("Other paths: \xe2\x80\xa6/cough with an integer (1 mute, 0 unmute), \xe2\x80\xa6/receivers (integer) and "
                                                   "\xe2\x80\xa6/name (text) for button labels, and /bridge/refresh to resend everything. "
                                                   "Feedback is sent when something changes and every 5 seconds."),
                           { 0, y, w, 40 } });
        y += 56;

        if (getHeight() != y)
            setSize (w, y);
    }

    void changed()
    {
        if (owner.onChanged)
            owner.onChanged();
        owner.repaint();
    }

private:
    class Row : public juce::Component
    {
    public:
        Row (Content& c, int s, juce::Colour col) : content (c), source (s), colour (col)
        {
            base = RemoteControl::addressFor (s);

            for (auto* b : { &copyToggle, &copyLive, &coughButton })
            {
                b->setFontHeight (12.0f);
                addAndMakeVisible (b);
            }

            copyToggle.setTooltip ("Copy " + base + "/cough/toggle");
            copyLive.setTooltip ("Copy " + base + "/live");
            copyToggle.onClick = [this] { copy (copyToggle, base + "/cough/toggle", "Copy toggle"); };
            copyLive.onClick = [this] { copy (copyLive, base + "/live", "Copy live"); };

            coughButton.setTooltip ("Cough mute: silences this mic to vMix (headphones are not affected)");
            coughButton.onClick = [this]
            {
                auto& e = content.engine;
                e.getSourceConfig (source).sendMuted = coughButton.getToggleState();
                e.applySourceConfig (source);
                if (content.owner.onSourceChanged)
                    content.owner.onSourceChanged();
                update();
            };

            update();
        }

        void update()
        {
            auto& e = content.engine;
            const auto st = RemoteControl::stateOf (e, source);
            const auto& p = pal();

            juce::String text;
            juce::Colour bg, fg;
            if (! st.sendEnabled)       { text = "NOT SENT";    bg = p.track;                    fg = p.dim; }
            else if (st.cough)          { text = "COUGH";       bg = p.danger;                   fg = onDanger(); }
            else if (! st.running)      { text = "OFFLINE";     bg = p.track;                    fg = p.dim; }
            else if (st.receivers == 0) { text = "NO RECEIVER"; bg = p.warn.withAlpha (0.18f);   fg = p.warn; }
            else                        { text = "LIVE";        bg = p.okBg;                     fg = p.okFg; }

            const auto n = e.getSourceConfig (source).name;
            if (text != chip || bg != chipBg || n != name || st.live != live)
            {
                live = st.live;
                chip = text;
                chipBg = bg;
                chipFg = fg;
                name = n;
                repaint();
            }

            if (coughButton.getToggleState() != st.cough)
                coughButton.setToggleState (st.cough, juce::dontSendNotification);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = pal();
            auto r = getLocalBounds().toFloat().withTrimmedLeft (20.0f);

            g.setColour (colour);
            g.fillEllipse (r.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f));
            r.removeFromLeft (10.0f);

            g.setColour (p.text);
            g.setFont (font (14.0f, true));
            g.drawText (name, r.removeFromLeft (120.0f), juce::Justification::centredLeft, true);

            g.setColour (p.dim);
            g.setFont (font (11.5f));
            g.drawText (RoutingModel::sourceTag (source), r.removeFromLeft (40.0f), juce::Justification::centredLeft, false);

            drawChip (g, r.removeFromLeft (96.0f).withSizeKeepingCentre (96.0f, 22.0f), chip, chipBg, chipFg);
            r.removeFromLeft (10.0f);

            // exactly what Companion's "live" feedback receives
            g.setFont (monoFont (12.0f));
            g.setColour (live ? p.green : p.faint);
            g.drawText (live ? "live 1" : "live 0", r.removeFromLeft (54.0f), juce::Justification::centredLeft, false);
            r.removeFromLeft (6.0f);

            g.setColour (p.text);
            g.setFont (monoFont (12.5f));
            g.drawText (base, r.withRight ((float) copyToggle.getX() - 8.0f), juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 10);
            coughButton.setBounds (r.removeFromRight (76));
            r.removeFromRight (8);
            copyLive.setBounds (r.removeFromRight (90));
            r.removeFromRight (8);
            copyToggle.setBounds (r.removeFromRight (104));
        }

    private:
        static void copy (FlatButton& b, const juce::String& text, const juce::String& label)
        {
            juce::SystemClipboard::copyTextToClipboard (text);
            b.setButtonText ("Copied");
            juce::Component::SafePointer<FlatButton> safe (&b);
            juce::Timer::callAfterDelay (1200, [safe, label] { if (safe != nullptr) safe->setButtonText (label); });
        }

        Content& content;
        const int source;
        const juce::Colour colour;
        juce::String base, name, chip;
        bool live = false;
        juce::Colour chipBg, chipFg;
        FlatButton copyToggle { "Copy toggle" }, copyLive { "Copy live" };
        FlatButton coughButton { "Cough", FlatButton::Kind::danger };
    };

    struct Step
    {
        int number = 0;
        juce::Rectangle<int> area;
        juce::TextLayout layout;
    };

    void applyPort()
    {
        auto c = remote.getConfig();
        const int port = portEditor.getText().getIntValue();
        if (juce::isPositiveAndBelow (port - 1, 65535) && port != c.port)
        {
            c.port = port;
            remote.setConfig (c);
            changed();
            layout();
            repaint();
        }
        portEditor.setText (juce::String (remote.getConfig().port), false);
    }

    void applyFeedback()
    {
        auto c = remote.getConfig();
        const auto text = feedbackEditor.getText().trim();
        if (text != c.feedbackTo)
        {
            c.feedbackTo = text;
            remote.setConfig (c);
            changed();
        }
    }

    CompanionPage& owner;
    AudioEngine& engine;
    RemoteControl& remote;

    Switch enableSwitch;
    juce::TextEditor portEditor, feedbackEditor;
    juce::OwnedArray<Row> rows;

    std::vector<Address> addresses;
    juce::Rectangle<int> computerArea;
    juce::String lastCommandText, feedbackText;
    juce::uint32 lastSlowTick = 0;

    std::vector<std::pair<juce::String, juce::Rectangle<int>>> headings, labels, notes;
    std::vector<juce::Rectangle<int>> cards, dividers;
    std::vector<Step> steps;
};

//==============================================================================
CompanionPage::CompanionPage (AudioEngine& e, RemoteControl& r) : engine (e), remote (r)
{
    content = std::make_unique<Content> (*this);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

CompanionPage::~CompanionPage()
{
    viewport.setViewedComponent (nullptr, false);
}

void CompanionPage::rebuild()
{
    content->rebuild();
    refresh();
}

void CompanionPage::refresh()
{
    content->refresh();
    repaint();
}

void CompanionPage::tick()
{
    content->tick();

    const auto& c = remote.getConfig();
    juce::String text;
    int kind;
    if (! c.enabled)                  { text = "Off"; kind = 2; }
    else if (remote.isListening())    { text = "Listening on UDP " + juce::String (c.port); kind = 0; }
    else                              { text = remote.getError().isNotEmpty() ? remote.getError() : juce::String ("Not listening"); kind = 1; }

    if (text != chipText || kind != chipKind)
    {
        chipText = text;
        chipKind = kind;
        repaint (0, 0, getWidth(), 90);
    }
}

void CompanionPage::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.fillAll (p.bg);

    g.setColour (p.text);
    g.setFont (font (22.0f, true));
    g.drawText ("Companion", 32, 24, 200, 30, juce::Justification::centredLeft, false);

    g.setColour (p.dim);
    g.setFont (font (13.0f));
    g.drawText ("Mute and unmute mics from Bitfocus Companion, with on-air feedback on the buttons.", 32, 54, getWidth() - 64, 20,
                juce::Justification::centredLeft, true);

    g.setFont (font (12.5f, true));
    const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), chipText)) + 40;
    auto chip = juce::Rectangle<float> (32.0f + 150.0f, 26.0f, (float) w, 26.0f);
    const juce::Colour bg = chipKind == 0 ? p.okBg : (chipKind == 1 ? p.danger.withAlpha (0.12f) : p.panel);
    const juce::Colour fg = chipKind == 0 ? p.okFg : (chipKind == 1 ? p.danger : p.dim);
    g.setColour (bg);
    g.fillRoundedRectangle (chip, 13.0f);
    if (chipKind == 2)
    {
        g.setColour (p.border);
        g.drawRoundedRectangle (chip.reduced (0.5f), 13.0f, 1.0f);
    }
    g.setColour (chipKind == 0 ? p.green : (chipKind == 1 ? p.danger : p.faint));
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ chip.getX() + 15.0f, chip.getCentreY() }));
    g.setColour (fg);
    g.drawText (chipText, chip.withTrimmedLeft (14.0f), juce::Justification::centred, false);
}

void CompanionPage::resized()
{
    auto area = getLocalBounds().withTrimmedTop (90).reduced (32, 0);
    viewport.setBounds (area.withWidth (juce::jmin (area.getWidth(), 820 + 14)));
    content->setSize (viewport.getWidth() - 14, content->getHeight());
    content->layout();
}
} // namespace ui

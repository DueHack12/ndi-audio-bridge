#include "MainComponent.h"
#include "engine/Settings.h"

MainComponent::MainComponent (AudioEngine& e, ui::LookAndFeel& laf)
    : engine (e), lookAndFeel (laf), routingPage (e), interfacesPage (e), settingsPage (e)
{
    addAndMakeVisible (routingPage);
    addChildComponent (interfacesPage);
    addChildComponent (settingsPage);
    addAndMakeVisible (sidebar);

    sidebar.onNavigate = [this] (ui::Sidebar::Page p) { showPage (p); };
    sidebar.onToggleTheme = [this] { setTheme (! ui::isDark()); };
    sidebar.onExpandedChanged = [this] { resized(); };

    routingPage.onChanged = [this] { markDirty(); };
    routingPage.onAddInterface = [this]
    {
        showPage (ui::Sidebar::Page::interfaces);
        interfacesPage.addInterface();
    };
    interfacesPage.onChanged = [this] { markDirty(); rebuildAll(); };
    settingsPage.onThemeChosen = [this] (bool dark) { setTheme (dark); };
    settingsPage.onChanged = [this] { markDirty(); };

    rebuildAll();
    setSize (1320, 820);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::saveNow()
{
    Settings::save (engine);
    dirty = false;
}

void MainComponent::markDirty()
{
    dirty = true;
    lastChangeMs = juce::Time::getMillisecondCounter();
}

void MainComponent::rebuildAll()
{
    lastStructure = engine.getStructureVersion();
    routingPage.rebuild();
    interfacesPage.rebuild();
    settingsPage.refresh();
}

void MainComponent::showPage (ui::Sidebar::Page p)
{
    page = p;
    sidebar.setPage (p);
    routingPage.setVisible (p == ui::Sidebar::Page::routing);
    interfacesPage.setVisible (p == ui::Sidebar::Page::interfaces);
    settingsPage.setVisible (p == ui::Sidebar::Page::settings);
    if (p == ui::Sidebar::Page::interfaces)
        interfacesPage.rebuild();
    if (p == ui::Sidebar::Page::settings)
        settingsPage.refresh();
}

void MainComponent::setTheme (bool dark)
{
    ui::setDark (dark);
    Settings::setDarkTheme (dark);
    lookAndFeel.applyTheme();
    sendLookAndFeelChange();
    rebuildAll(); // colours baked into child controls
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setBackgroundColour (ui::pal().bg);
    repaint();
}

void MainComponent::updateResources()
{
    monitor.update();
    const auto& snap = monitor.get();

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto sent = engine.getSendHub().getBytesSent();
    const auto received = engine.getReceiver().getBytesReceived();
    double txBps = 0.0, rxBps = 0.0;
    if (lastNdiTime > 0.0 && now > lastNdiTime)
    {
        txBps = (double) (sent - lastSentBytes) * 8.0 / (now - lastNdiTime);
        rxBps = (double) (received - lastReceivedBytes) * 8.0 / (now - lastNdiTime);
    }
    lastSentBytes = sent;
    lastReceivedBytes = received;
    lastNdiTime = now;

    ui::ResourceInfo r;
    r.appCpu = snap.appCpu;
    r.systemCpu = snap.systemCpu;
    r.audioLoad = engine.getAudioLoad();
    r.ndiTxBitsPerSec = txBps;
    r.ndiRxBitsPerSec = rxBps;
    r.netRxBitsPerSec = snap.netRxBitsPerSec;
    r.netTxBitsPerSec = snap.netTxBitsPerSec;
    r.linkBitsPerSec = snap.linkBitsPerSec;
    sidebar.setResources (r);
}

void MainComponent::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();

    sidebar.tick();

    if (engine.getStructureVersion() != lastStructure)
        rebuildAll();

    levels.read (engine);
    if (page == ui::Sidebar::Page::routing)
        routingPage.tick (levels);
    else if (page == ui::Sidebar::Page::interfaces)
        interfacesPage.tick();

    if (now - lastSecondMs >= 1000)
    {
        lastSecondMs = now;
        updateResources();
    }

    if (now - lastRetryMs >= 5000)
    {
        lastRetryMs = now;
        engine.retryInterfaces();
        if (engine.getComputerAudioDeviceName().isNotEmpty() && ! engine.isComputerAudioRunning())
            engine.openComputerAudioDevice (engine.getComputerAudioDeviceName());
    }

    if (dirty && now - lastChangeMs > 1000)
        saveNow();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (ui::pal().bg);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    const int sidebarW = sidebar.isExpanded() ? ui::Sidebar::expandedWidth + ui::Sidebar::shadowWidth
                                              : ui::Sidebar::collapsedWidth;
    sidebar.setBounds (0, 0, sidebarW, getHeight());
    sidebar.toFront (false);

    r.removeFromLeft (ui::Sidebar::collapsedWidth);
    routingPage.setBounds (r);
    interfacesPage.setBounds (r);
    settingsPage.setBounds (r);
}

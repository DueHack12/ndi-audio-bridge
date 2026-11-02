#pragma once

#include "CompanionPage.h"
#include "Pages.h"
#include "RoutingPage.h"
#include "Sidebar.h"
#include "engine/SystemMonitor.h"

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent (AudioEngine&, RemoteControl&, ui::LookAndFeel&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Writes settings to disk immediately (used on quit). */
    void saveNow();

private:
    void timerCallback() override;
    void showPage (ui::Sidebar::Page);
    void setTheme (bool dark);
    void markDirty();
    void rebuildAll();
    void updateResources();

    AudioEngine& engine;
    RemoteControl& remote;
    ui::LookAndFeel& lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    ui::Sidebar sidebar;
    ui::RoutingPage routingPage;
    ui::InterfacesPage interfacesPage;
    ui::CompanionPage companionPage;
    ui::SettingsPage settingsPage;
    ui::Sidebar::Page page = ui::Sidebar::Page::routing;

    ui::Levels levels;
    SystemMonitor monitor;
    int lastStructure = -1;
    bool dirty = false;
    juce::uint32 lastChangeMs = 0, lastSecondMs = 0, lastRetryMs = 0;
    juce::uint64 lastSentBytes = 0, lastReceivedBytes = 0;
    double lastNdiTime = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

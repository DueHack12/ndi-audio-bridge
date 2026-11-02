#include "engine/AudioEngine.h"
#include "engine/RemoteControl.h"
#include "engine/Settings.h"
#include "ui/MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

class NdiAudioBridgeApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        ui::setDark (Settings::isDarkTheme());
        lookAndFeel = std::make_unique<ui::LookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        engine = std::make_unique<AudioEngine>();
        Settings::restore (*engine);
        engine->startup();
        engine->addDefaultInterfaceIfNone();

        if (auto computer = Settings::getSavedComputerDevice(); computer.isNotEmpty())
            engine->openComputerAudioDevice (computer);

        remote = std::make_unique<RemoteControl> (*engine);
        Settings::restore (*remote);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *remote, *lookAndFeel);
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
            mainWindow->content->saveNow();

        mainWindow.reset();
        remote.reset();
        engine.reset(); // stops audio, NDI threads, then unloads NDI
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    struct MainWindow : public juce::DocumentWindow
    {
        MainWindow (const juce::String& name, AudioEngine& audioEngine, RemoteControl& remoteControl, ui::LookAndFeel& laf)
            : DocumentWindow (name, ui::pal().bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent (audioEngine, remoteControl, laf);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (1100, 700, 3000, 2000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        MainComponent* content = nullptr; // owned by the window
    };

    std::unique_ptr<ui::LookAndFeel> lookAndFeel;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<RemoteControl> remote;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (NdiAudioBridgeApplication)

#include "engine/AudioEngine.h"
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
        lookAndFeel.setColourScheme ({ 0xff111317, 0xff1a1d23, 0xff1a1d23,
                                       0xff2a2f38, 0xffe7e9ec, 0xff2f7cf6,
                                       0xffffffff, 0xff2f7cf6, 0xffe7e9ec });
        lookAndFeel.setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff23272f));
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        engine = std::make_unique<AudioEngine>();
        Settings::restoreConfig (*engine);

        auto interfaceState = Settings::getSavedInterfaceState();
        engine->startup (interfaceState.get());

        if (auto computer = Settings::getSavedComputerDevice(); computer.isNotEmpty())
            engine->openComputerAudioDevice (computer);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
            mainWindow->content->saveNow();

        mainWindow.reset();
        engine.reset(); // stops audio, NDI threads, then unloads NDI
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    struct MainWindow : public juce::DocumentWindow
    {
        MainWindow (const juce::String& name, AudioEngine& audioEngine)
            : DocumentWindow (name, juce::Colour (0xff111317), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent (audioEngine);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (1100, 720, 3000, 2000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        MainComponent* content = nullptr; // owned by the window
    };

    juce::LookAndFeel_V4 lookAndFeel;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (NdiAudioBridgeApplication)

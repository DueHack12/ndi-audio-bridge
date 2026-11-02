#pragma once

#include "RoutingModel.h"
#include "Widgets.h"

namespace ui
{
/** Add, remove and configure audio interfaces (up to four). */
class InterfacesPage : public juce::Component
{
public:
    explicit InterfacesPage (AudioEngine&);
    ~InterfacesPage() override;

    void rebuild();
    void tick();
    void addInterface();

    std::function<void()> onChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Card;
    AudioEngine& engine;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<Card> cards;
    FlatButton addButton { "Add audio interface", FlatButton::Kind::ghost };
    FlatButton rescanButton { "Rescan devices" };
};

/** Theme, buffers, NDI status, about. */
class SettingsPage : public juce::Component
{
public:
    explicit SettingsPage (AudioEngine&);

    void refresh();
    std::function<void (bool dark)> onThemeChosen;
    std::function<void()> onChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider& makeMsSlider (juce::Slider&, double min, double max, std::function<void (double)>);

    AudioEngine& engine;
    FlatButton lightButton { "Light", FlatButton::Kind::toggle }, darkButton { "Dark", FlatButton::Kind::toggle };
    juce::Slider returnSlider, computerSlider, crossSlider;
    juce::HyperlinkButton githubLink { "github.com/DueHack12/ndi-audio-bridge",
                                       juce::URL ("https://github.com/DueHack12/ndi-audio-bridge") };
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> headings, labels, notes;
};
} // namespace ui

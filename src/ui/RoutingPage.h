#pragma once

#include "MixerPanel.h"
#include "PatchView.h"
#include "SourceInspector.h"

namespace ui
{
/** Header with interface chips, the wire view, the mixer, and the source pop-out. */
class RoutingPage : public juce::Component
{
public:
    explicit RoutingPage (AudioEngine&);

    void rebuild();
    void tick (const Levels&);

    std::function<void()> onChanged;          // something to save
    std::function<void()> onAddInterface;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void openInspector (int source);
    void closeInspector();
    void routingChanged();
    void refreshNames();

    AudioEngine& engine;
    PatchView patch;
    MixerPanel mixer;
    SourceInspector inspector;
    FlatButton addInterfaceButton { "Add interface", FlatButton::Kind::ghost };

    std::vector<int> sources;
    std::vector<MixRef> mixes;
    int selectedSource = -1, selectedMix = 0;
    juce::Rectangle<int> headerArea;
};
} // namespace ui

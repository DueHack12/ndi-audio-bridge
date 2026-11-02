#pragma once

#include "RoutingModel.h"
#include "Widgets.h"

namespace ui
{
/** Channel-strip mixer for the selected headphone mix: one tab per mix, one
    strip per source (on/off, L/R, level) plus the mix's master. */
class MixerPanel : public juce::Component
{
public:
    explicit MixerPanel (AudioEngine&);
    ~MixerPanel() override;

    void rebuild (const std::vector<int>& sources, const std::vector<MixRef>& mixes,
                  std::function<juce::Colour (int)> colourOf);
    void setSelectedMix (int index);
    int getSelectedMix() const noexcept { return selected; }
    void refreshValues();
    void updateLevels (const Levels&);

    std::function<void (int)> onMixSelected;
    std::function<void()> onRoutingChanged;     // a route or level changed
    std::function<void()> onStructureChanged;   // stereo/2x mono changed (the mix list changes)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class SourceStrip;
    class MasterStrip;

    void layoutStrips();
    void updateModeButtons();

    AudioEngine& engine;
    std::vector<int> sources;
    std::vector<MixRef> mixes;
    int selected = 0;
    std::vector<juce::Rectangle<int>> tabRects;

    FlatButton stereoButton { "Stereo", FlatButton::Kind::toggle };
    FlatButton monoButton { juce::String::fromUTF8 ("2\xc3\x97 mono"), FlatButton::Kind::toggle };

    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<SourceStrip> strips;
    std::unique_ptr<MasterStrip> master;
};
} // namespace ui

#pragma once

#include "RoutingModel.h"
#include "Widgets.h"

namespace ui
{
/** Sources on the left, headphone mixes on the right, wires between them.
    Click a source to inspect it, click a mix to edit it in the mixer below,
    drag from a source onto a mix to add a route. */
class PatchView : public juce::Component
{
public:
    explicit PatchView (AudioEngine& e) : engine (e) {}

    void rebuild (const std::vector<int>& sources, const std::vector<MixRef>& mixes);
    void setSelection (int source, int mixIndex);
    void updateLevels (const Levels&);

    std::function<void (int source)> onSourceClicked;
    std::function<void (int mixIndex)> onMixClicked;
    std::function<void (int source, int mixIndex)> onRouteDropped;

    void paint (juce::Graphics&) override;
    void resized() override { layoutItems(); gridCache = {}; }
    void lookAndFeelChanged() override { gridCache = {}; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    /** Colour used for a source everywhere (pills, wires, strips). */
    juce::Colour colourFor (int source) const;

private:
    struct Src
    {
        int source = 0, order = 0;
        juce::String name, tag;
        juce::Rectangle<float> box;
        float level = -60.0f;
    };

    struct Mix
    {
        MixRef ref;
        juce::String name, sub;
        juce::Rectangle<float> box;
    };

    struct GroupLabel
    {
        juce::String text;
        juce::Point<float> pos;
    };

    void layoutItems();
    int hitSource (juce::Point<float>) const;
    int hitMix (juce::Point<float>) const;
    static juce::Point<float> portOf (const Src& s) { return { s.box.getRight(), s.box.getCentreY() }; }
    static juce::Point<float> portOf (const Mix& m) { return { m.box.getX(), m.box.getCentreY() }; }
    void drawWire (juce::Graphics&, juce::Point<float> a, juce::Point<float> b, juce::Colour, float width, bool dashed) const;

    AudioEngine& engine;
    std::vector<Src> srcs;
    std::vector<Mix> mixes;
    std::vector<GroupLabel> srcLabels, mixLabels;
    int selectedSource = -1, selectedMix = 0;

    int pressedSource = -1, hoverMix = -1;
    bool dragging = false;
    juce::Point<float> downPos, dragPos;
    juce::Image gridCache;
};
} // namespace ui

#pragma once

#include "RoutingModel.h"
#include "Widgets.h"

namespace ui
{
/** The pop-out panel for one source: name, NDI send, cough, trim, device/NDI
    source picker and every mix the source feeds, each with its own level. */
class SourceInspector : public juce::Component
{
public:
    explicit SourceInspector (AudioEngine&);
    ~SourceInspector() override;

    void show (int source, juce::Colour colour, const std::vector<MixRef>& mixes);
    int getSource() const noexcept { return source; }
    void refreshRoutes();
    void tick (const Levels&);

    std::function<void()> onClose;
    std::function<void()> onRoutingChanged;   // routes / levels changed
    std::function<void()> onSourceChanged;    // name, send settings, device changed

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int panelWidth = 330;

private:
    class Body;
    AudioEngine& engine;
    int source = -1;
    juce::Viewport viewport;
    std::unique_ptr<Body> body;
};
} // namespace ui

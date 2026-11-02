#pragma once

#include "RoutingModel.h"
#include "Widgets.h"

class RemoteControl;

namespace ui
{
/** Bitfocus Companion (OSC) control: connection settings, setup steps and the
    OSC paths for every mic, with live status and a cough button to test with. */
class CompanionPage : public juce::Component
{
public:
    CompanionPage (AudioEngine&, RemoteControl&);
    ~CompanionPage() override;

    /** Sources changed (interfaces added/removed, names edited). */
    void rebuild();
    /** Settings changed elsewhere (e.g. the feedback address was learned). */
    void refresh();
    /** ~30 times a second while visible. */
    void tick();

    std::function<void()> onChanged;        // OSC settings changed: save
    std::function<void()> onSourceChanged;  // a cough button here was used

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content;
    AudioEngine& engine;
    RemoteControl& remote;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    juce::String chipText;
    int chipKind = 0; // 0 listening, 1 error, 2 off
};
} // namespace ui

#pragma once

#include "engine/AudioEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

class LevelMeter;
class SendRow;
class MatrixPanel;

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    explicit MainComponent (AudioEngine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Writes settings to disk immediately (used on quit). */
    void saveNow();

private:
    void timerCallback() override;

    void markDirty();
    void rebuildForDevice();
    void sourceNamesChanged();
    void showInterfaceSettings();
    void refreshReturnSources (bool force);
    void refreshComputerDevices();
    void updateStatusText();

    AudioEngine& engine;
    juce::TooltipWindow tooltipWindow { this, 600 };

    // Header
    juce::TextButton interfaceButton { "Audio Interface..." };
    juce::Label interfaceSummary;

    // Send panel (one row per interface input + computer audio)
    juce::Viewport sendViewport;
    juce::Component sendRowsHolder;
    juce::OwnedArray<SendRow> sendRows;

    // vMix return panel
    juce::ComboBox returnCombo;
    juce::Slider returnGain, returnBuffer;
    std::unique_ptr<LevelMeter> returnMeter;
    juce::Label returnStatus, returnBufferLabel;
    int lastFinderChange = -1;

    // Computer audio panel
    juce::ComboBox computerCombo;
    juce::TextButton computerRescan { "Rescan" };
    juce::Slider computerBuffer;
    juce::Label computerStatus, computerHint, computerBufferLabel;

    // Headphone matrix
    std::unique_ptr<MatrixPanel> matrix;

    // Layout + state
    juce::Rectangle<int> sendArea, sendHeadingRow, returnArea, computerArea, matrixArea, headerArea;
    int builtInputs = -1, builtBuses = -1;
    bool dirty = false;
    juce::uint32 lastChangeMs = 0, lastComputerRetryMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

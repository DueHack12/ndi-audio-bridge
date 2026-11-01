#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

/** Watches the network for NDI sources on a background thread. */
class NdiFinder : private juce::Thread
{
public:
    NdiFinder();
    ~NdiFinder() override;

    void start();
    void stop();

    /** Any thread. Full names such as "VMIX-PC (vMix - Output 1)". */
    juce::StringArray getSources() const;

    /** Bumped every time the list changes, so the UI can cheaply poll. */
    int getChangeCount() const noexcept { return changeCount.load(); }

private:
    void run() override;

    mutable juce::CriticalSection lock;
    juce::StringArray sources;
    std::atomic<int> changeCount { 0 };
};

#pragma once

#include "dsp/DriftFifo.h"

#include <juce_core/juce_core.h>
#include <atomic>

/** Receives audio from one NDI source (e.g. a vMix bus/output) on its own
    thread and feeds it into a DriftFifo that the interface callback pulls from.

    The receiver asks NDI for audio only, so a vMix output that also carries
    video costs no video decoding here.
*/
class NdiReceiver : private juce::Thread
{
public:
    explicit NdiReceiver (DriftFifo& destination, juce::String receiverName = "NDI Audio Bridge Return");
    ~NdiReceiver() override;

    void start();
    void stop();

    /** Message thread. Full NDI name, e.g. "VMIX-PC (vMix - Output 1)". Empty = off. */
    void setSource (const juce::String& fullNdiName);
    juce::String getSource() const;

    // Status for the UI
    bool isConnected() const noexcept        { return connected.load(); }
    bool isReceiving() const noexcept;
    int getSourceChannels() const noexcept   { return sourceChannels.load(); }
    int getSourceSampleRate() const noexcept { return sourceRate.load(); }

    /** Which source channels feed left/right (0-based). Defaults to 0/1. */
    void setChannelMap (int leftChannel, int rightChannel) noexcept;

private:
    void run() override;

    DriftFifo& fifo;
    const juce::String recvName;

    mutable juce::SpinLock sourceLock;
    juce::String sourceName;
    std::atomic<int> sourceVersion { 0 };

    std::atomic<bool> connected { false };
    std::atomic<juce::uint32> lastAudioMs { 0 };
    std::atomic<int> sourceChannels { 0 }, sourceRate { 0 };
    std::atomic<int> leftChan { 0 }, rightChan { 1 };
};

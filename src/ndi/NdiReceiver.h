#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

/** Receives audio from one NDI source (e.g. a vMix bus/output) on its own
    thread and hands each frame to `onAudio` (the engine fans it out to every
    interface that plays it).

    The receiver asks NDI for audio only, so a vMix output that also carries
    video costs no video decoding here.
*/
class NdiReceiver : private juce::Thread
{
public:
    /** Called on the receive thread with planar L/R (R may equal L for mono). */
    using AudioSink = std::function<void (const float* l, const float* r, int numSamples, double sampleRate)>;

    explicit NdiReceiver (juce::String receiverName = "NDI Audio Bridge Return");

    /** Set before start(). onReset is called when the source changes. */
    AudioSink onAudio;
    std::function<void()> onReset;

    /** Payload bytes received so far (for the network glance). */
    juce::uint64 getBytesReceived() const noexcept { return bytesReceived.load(); }
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

    const juce::String recvName;
    std::atomic<juce::uint64> bytesReceived { 0 };

    mutable juce::SpinLock sourceLock;
    juce::String sourceName;
    std::atomic<int> sourceVersion { 0 };

    std::atomic<bool> connected { false };
    std::atomic<juce::uint32> lastAudioMs { 0 };
    std::atomic<int> sourceChannels { 0 }, sourceRate { 0 };
    std::atomic<int> leftChan { 0 }, rightChan { 1 };
};

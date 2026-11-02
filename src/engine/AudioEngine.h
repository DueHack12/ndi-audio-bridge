#pragma once

#include "dsp/DriftFifo.h"
#include "ndi/NdiFinder.h"
#include "ndi/NdiReceiver.h"
#include "ndi/NdiSendHub.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>

namespace bridge
{
constexpr int maxInputs = 16;                 // interface inputs we handle
constexpr int maxBuses = 8;                   // stereo output pairs (Out 1/2, Out 3/4, ...)
constexpr int computerSource = maxInputs;     // matrix row / send slot for computer audio
constexpr int returnSource = maxInputs + 1;   // matrix row for the NDI return
constexpr int numSources = maxInputs + 2;

/** Peak meter value written by an audio thread, read (and reset) by the UI. */
struct Meter
{
    std::atomic<float> peak { 0.0f };

    void update (float v) noexcept
    {
        if (v > peak.load (std::memory_order_relaxed))
            peak.store (v, std::memory_order_relaxed);
    }

    float readAndReset() noexcept { return peak.exchange (0.0f); }
};

/** Message-thread view of one row (an input, computer audio, or the return). */
struct SourceConfig
{
    juce::String name;          // NDI source name for sends; label for the return
    float gainDb = 0.0f;
    bool sendEnabled = false;   // send to NDI (inputs + computer audio only)
    bool sendStereo = true;     // send as 2-channel (dual-mono for mic inputs)
    bool sendMuted = false;     // "cough": mutes the NDI send, not the headphones
};

/** One output pair (Out 1/2, Out 3/4, ...). In stereo mode it is one stereo mix;
    in dual-mono mode each output is its own mono mix (e.g. two separate headphone
    feeds). Either way every source can be routed to each side independently. */
struct BusConfig
{
    bool stereo = true;
    juce::String name, nameRight;          // nameRight: the second output's mix in dual-mono mode
    float gainDb = 0.0f, gainDbRight = 0.0f;
    bool routes[2][numSources] {};         // [0] = left / first output, [1] = right / second output
};
} // namespace bridge

/** All audio routing:

      interface inputs ---------+--> NDI sends (one source per input) --> vMix
                                |
      computer audio device ----+--> NDI send "Computer Audio"
        (virtual output)        |
                                v
      NDI return from vMix --> headphone matrix --> interface outputs (pairs)
*/
class AudioEngine : private juce::AudioIODeviceCallback,
                    private juce::ChangeListener
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Opens the interface (restoring a saved setup if given) and starts NDI. */
    void startup (const juce::XmlElement* savedInterfaceState);
    void shutdown();

    // ---------------------------------------------------------------- interface device
    juce::AudioDeviceManager& getInterfaceManager() noexcept { return interfaceManager; }
    int getNumInputs() const noexcept   { return numInputs.load(); }
    int getNumBuses() const noexcept    { return numBuses.load(); }
    juce::String getInputChannelName (int index) const;
    juce::String getBusChannelLabel (int bus) const;   // e.g. "Out 1/2"
    juce::String getOutputChannelLabel (int bus, int side) const; // e.g. "Out 2"
    bool busHasSecondOutput (int bus) const;           // false for the last pair of an odd count
    juce::String getInterfaceSummary() const;

    // ---------------------------------------------------------------- computer audio device
    juce::StringArray getComputerAudioDeviceNames();
    /** Empty name closes it. Returns an error message or empty on success. */
    juce::String openComputerAudioDevice (const juce::String& inputDeviceName);
    juce::String getComputerAudioDeviceName() const { return computerDeviceName; }
    bool isComputerAudioRunning() const noexcept   { return computerRunning.load(); }
    juce::String getComputerAudioSummary() const;

    // ---------------------------------------------------------------- configuration (message thread)
    bridge::SourceConfig& getSourceConfig (int source)  { return sourceConfigs[(size_t) source]; }
    bridge::BusConfig& getBusConfig (int bus)           { return busConfigs[(size_t) bus]; }

    /** Push a changed SourceConfig into the audio/NDI side. */
    void applySourceConfig (int source);
    void applyBusConfig (int bus);

    void setReturnSource (const juce::String& fullNdiName);
    juce::String getReturnSource() const { return returnSourceName; }
    void setReturnBufferMs (double ms);
    double getReturnBufferMs() const { return returnBufferMs; }
    void setComputerBufferMs (double ms);
    double getComputerBufferMs() const { return computerBufferMs; }

    // ---------------------------------------------------------------- live status
    bridge::Meter& getSourceMeter (int source) noexcept           { return sourceMeters[(size_t) source]; }
    bridge::Meter& getBusMeter (int bus, int side) noexcept        { return busMeters[(size_t) (bus * 2 + side)]; }
    NdiSendHub& getSendHub() noexcept      { return sendHub; }
    NdiReceiver& getReceiver() noexcept    { return receiver; }
    NdiFinder& getFinder() noexcept        { return finder; }
    DriftFifo& getReturnFifo() noexcept    { return returnFifo; }
    DriftFifo& getComputerFifo() noexcept  { return computerFifo; }

    static juce::String defaultSourceName (int source);

    /** For tests: drive the interface callback with a fake device. */
    juce::AudioIODeviceCallback& getInterfaceCallback() noexcept { return *this; }

private:
    // Interface callback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void selectTascamIfPresent();

    void processChunk (const float* const* in, int numIn, float* const* out, int numOut, int offset, int n) noexcept;

    // Computer audio (second device, its own clock)
    struct ComputerCallback : juce::AudioIODeviceCallback
    {
        explicit ComputerCallback (AudioEngine& e) : engine (e) {}
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override;

        AudioEngine& engine;
        double sampleRate = 48000.0;
        juce::HeapBlock<float> scratchL, scratchR;
        int scratchSize = 0;
    };

    juce::AudioIODeviceType* getComputerDeviceType();

    // Devices
    juce::AudioDeviceManager interfaceManager;
    juce::OwnedArray<juce::AudioIODeviceType> computerTypes;
    std::unique_ptr<juce::AudioIODevice> computerDevice;
    ComputerCallback computerCallback { *this };
    juce::String computerDeviceName;
    std::atomic<bool> computerRunning { false };

    // NDI
    NdiSendHub sendHub;
    DriftFifo returnFifo, computerFifo;
    NdiReceiver receiver { returnFifo };
    NdiFinder finder;

    // Message-thread configuration
    std::array<bridge::SourceConfig, bridge::numSources> sourceConfigs;
    std::array<bridge::BusConfig, bridge::maxBuses> busConfigs;
    juce::String returnSourceName;
    double returnBufferMs = 60.0, computerBufferMs = 30.0;

    // Audio-thread parameters
    std::array<std::atomic<float>, bridge::numSources> sourceGain;
    // Per output channel (bus * 2 + side)
    std::array<std::atomic<float>, bridge::maxBuses * 2> outGain;
    std::array<std::array<std::atomic<bool>, bridge::maxBuses * 2>, bridge::numSources> routes;
    std::array<std::atomic<bool>, bridge::maxBuses> busStereo;

    std::array<bridge::Meter, bridge::numSources> sourceMeters;
    std::array<bridge::Meter, bridge::maxBuses * 2> busMeters;

    // Interface callback state (set in audioDeviceAboutToStart)
    static constexpr int maxDeviceChannels = 64;
    std::atomic<int> numInputs { 0 }, numBuses { 0 };
    std::array<int, maxDeviceChannels> activeInputChannel {};   // callback index -> device channel
    std::array<int, maxDeviceChannels> outputIndexForChannel {}; // device channel -> callback index or -1
    int numActiveInputs = 0;
    double sampleRate = 48000.0;
    juce::AudioBuffer<float> inputScratch, mixScratch;
    std::array<bool, bridge::maxInputs> inputPresent {};
    static constexpr int chunkSize = 1024;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

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
constexpr int maxInterfaces = 8;                                         // device slots (an input, an output, or both)
constexpr int maxInsPerInterface = 16;
constexpr int maxPairsPerInterface = 8;                                   // 16 outputs
constexpr int computerSource = maxInterfaces * maxInsPerInterface;       // 128
constexpr int returnSource = computerSource + 1;                         // 129
constexpr int numSources = returnSource + 1;                             // 130
constexpr int numBuses = maxInterfaces * maxPairsPerInterface;           // 64 output pairs
constexpr int numOutChannels = numBuses * 2;                             // 128

constexpr int sourceIndex (int iface, int ch) noexcept   { return iface * maxInsPerInterface + ch; }
constexpr int interfaceOfSource (int s) noexcept         { return s < computerSource ? s / maxInsPerInterface : -1; }
constexpr int channelOfSource (int s) noexcept           { return s % maxInsPerInterface; }
constexpr int busIndex (int iface, int pair) noexcept    { return iface * maxPairsPerInterface + pair; }
constexpr int interfaceOfBus (int b) noexcept            { return b / maxPairsPerInterface; }
constexpr int pairOfBus (int b) noexcept                 { return b % maxPairsPerInterface; }

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

/** Message-thread view of one source (an interface input, computer audio, or the return). */
struct SourceConfig
{
    juce::String name;          // NDI source name for sends; label for the return
    float gainDb = 0.0f;        // trim
    bool sendEnabled = true;    // send to NDI (inputs + computer audio only)
    bool sendStereo = true;     // 2-channel NDI source (dual-mono for mic inputs)
    bool sendMuted = false;     // "cough": mutes the NDI send, not the headphones
};

/** One output pair (Out 1/2, Out 3/4, ...) of an interface. In stereo mode it is one
    stereo mix; in dual-mono mode each output is its own mono mix. Either way every
    source can be routed to each side independently, at its own level. */
struct BusConfig
{
    bool stereo = true;
    juce::String name, nameRight;          // nameRight: the second output's mix in dual-mono mode
    float gainDb = 0.0f, gainDbRight = 0.0f;
    bool routes[2][numSources] {};         // [0] = left / first output, [1] = right / second output
    float levelsDb[2][numSources] {};      // per-source level into each output (0 dB default)
};

/** Which device(s) a slot uses: an input device, an output device, or both when they are
    the same hardware (then they share one clock and one callback: lowest latency). */
struct InterfaceConfig
{
    juce::String typeName, inputName, outputName;
    double sampleRate = 48000.0;
    int bufferSize = 128;
    int maxIns = 0, maxOuts = 0;   // channels used; 0 = all (up to 16)

    juce::String getKey() const { return outputName.isNotEmpty() ? outputName : inputName; }
};
} // namespace bridge

/** All audio routing, for up to four audio interfaces at once:

      interface inputs ---------+--> NDI sends (one source per input) --> vMix
                                |
      computer audio device ----+--> NDI send "Computer Audio"
        (virtual output)        |
                                v
      NDI return from vMix --> headphone mixes --> any interface's outputs

    Each interface runs on its own clock. Its inputs go straight to NDI. Audio that
    crosses from one clock to another (another interface, the computer audio, the
    NDI return) goes through a drift-correcting DriftFifo per destination interface.
*/
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    void startup();
    void shutdown();

    // ---------------------------------------------------------------- interfaces (message thread)
    juce::StringArray getDeviceTypeNames();
    juce::String getDefaultDeviceTypeName();
    juce::StringArray getDeviceNames (const juce::String& typeName, bool inputs, bool rescan = false);
    juce::Array<double> getSampleRates (int slot) const;
    juce::Array<int> getBufferSizes (int slot) const;

    /** Uses the first free slot; returns it, or -1 when all are taken. Opens it straight away. */
    int addInterface (const bridge::InterfaceConfig&);
    /** Restores a saved interface into a specific slot without opening it (call openInterface later). */
    void setInterfaceConfig (int slot, const bridge::InterfaceConfig&);
    juce::String openInterface (int slot);
    void removeInterface (int slot);
    /** Picks a sensible first interface (a TASCAM if connected) when none are configured. */
    void addDefaultInterfaceIfNone();

    bool isInterfaceUsed (int slot) const noexcept;
    bool isInterfaceRunning (int slot) const noexcept;
    const bridge::InterfaceConfig& getInterfaceConfig (int slot) const;
    juce::String getInterfaceName (int slot) const;

    // ---------------------------------------------------------------- inputs and outputs as separate devices
    // An input and an output on the same device (same driver and name) share one slot, so audio
    // between them needs no clock bridge. Anything else gets its own slot and clock.
    bool hasInput (int slot) const noexcept;
    bool hasOutput (int slot) const noexcept;
    juce::String getInputDeviceName (int slot) const;
    juce::String getOutputDeviceName (int slot) const;
    /** Returns the slot used, or -1 when all slots are taken or that device is already an input. */
    int addInput (const juce::String& typeName, const juce::String& deviceName);
    int addOutput (const juce::String& typeName, const juce::String& deviceName);
    /** Moves a slot's input to another device. Names, cough, routing and levels go with it.
        Returns the slot it ends up in, or -1 if that device is already used as an input. */
    int setInputDevice (int slot, const juce::String& typeName, const juce::String& deviceName);
    int setOutputDevice (int slot, const juce::String& typeName, const juce::String& deviceName);
    void removeInput (int slot);
    void removeOutput (int slot);
    /** Older versions could pair an input and an output of different devices in one slot;
        splits those into two slots. Call after Settings::restore, before startup. */
    void splitMixedSlots();
    /** "48.0 kHz · 128 samples · 4 in / 4 out", or why it isn't running. direction: 0 both, 1 inputs, 2 outputs. */
    juce::String getInterfaceStatus (int slot, int direction = 0) const;
    int getNumInputs (int slot) const noexcept;
    int getNumOutputs (int slot) const noexcept;
    int getNumPairs (int slot) const noexcept { return (getNumOutputs (slot) + 1) / 2; }
    double getInterfaceSampleRate (int slot) const noexcept;
    int getDeviceInputCount (int slot) const noexcept;    // channels the hardware offers
    int getDeviceOutputCount (int slot) const noexcept;

    /** Bumped whenever interfaces, channel counts or mix modes change, so the UI can rebuild. */
    int getStructureVersion() const noexcept { return structureVersion.load(); }
    void bumpStructure() noexcept { structureVersion.fetch_add (1); }

    /** Re-opens interfaces that are configured but not running (unplugged, busy). Call every few seconds. */
    void retryInterfaces();

    // ---------------------------------------------------------------- sources & mixes (message thread)
    bool sourceExists (int source) const noexcept;
    bridge::SourceConfig& getSourceConfig (int source)  { return sourceConfigs[(size_t) source]; }
    void applySourceConfig (int source);
    juce::String getSourceDeviceLabel (int source) const;      // e.g. "TASCAM US-4x4HR · IN 2"
    juce::String getSourceKey (int source) const;              // stable key for saving
    int findSourceByKey (const juce::String& key) const;
    static juce::String defaultSourceName (int source);

    bool busExists (int bus) const noexcept;
    bool busHasSecondOutput (int bus) const noexcept;
    bridge::BusConfig& getBusConfig (int bus)           { return busConfigs[(size_t) bus]; }
    void applyBusConfig (int bus);
    juce::String getBusLabel (int bus) const;                  // "Out 1/2"
    juce::String getOutputLabel (int bus, int side) const;     // "Out 2"
    juce::String getBusKey (int bus) const;
    int findBusByKey (const juce::String& key) const;

    // ---------------------------------------------------------------- computer audio & NDI return
    juce::StringArray getComputerAudioDeviceNames();
    juce::String openComputerAudioDevice (const juce::String& inputDeviceName);
    juce::String getComputerAudioDeviceName() const { return computerDeviceName; }
    bool isComputerAudioRunning() const noexcept   { return computerRunning.load(); }
    juce::String getComputerAudioSummary() const;

    void setReturnSource (const juce::String& fullNdiName);
    juce::String getReturnSource() const { return returnSourceName; }
    void setReturnBufferMs (double ms);
    double getReturnBufferMs() const { return returnBufferMs; }
    void setComputerBufferMs (double ms);
    double getComputerBufferMs() const { return computerBufferMs; }
    void setCrossBufferMs (double ms);
    double getCrossBufferMs() const { return crossBufferMs; }

    /** Stats of the return as heard by the first interface that plays it (null if none). */
    const DriftFifo* getReturnStats() const;

    // ---------------------------------------------------------------- live status
    bridge::Meter& getSourceMeter (int source) noexcept { return sourceMeters[(size_t) source]; }
    bridge::Meter& getOutputMeter (int bus, int side) noexcept { return outputMeters[(size_t) (bus * 2 + side)]; }
    float getAudioLoad() const noexcept;     // 0..1, busiest audio callback
    NdiSendHub& getSendHub() noexcept       { return sendHub; }
    NdiReceiver& getReceiver() noexcept     { return receiver; }
    NdiFinder& getFinder() noexcept         { return finder; }

    /** For tests: the callback of a slot, to drive with a fake device. */
    juce::AudioIODeviceCallback& getInterfaceCallback (int slot);

private:
    struct Interface;
    struct ComputerCallback;
    friend struct Interface;
    friend struct ComputerCallback;

    juce::AudioIODeviceType* findType (const juce::String& typeName);
    DriftFifo* ensureCross (int source, int consumerSlot);
    double targetMsForSource (int source) const;
    void fanOut (int source, const float* l, const float* r, int n, double rate, int skipSlot) noexcept;
    void openSendsForSlot (int slot);
    void resetSlotDefaults (int slot);
    void resetInputDefaults (int slot);
    void resetOutputDefaults (int slot);
    int findFreeSlot() const;
    int findSlot (const juce::String& typeName, const juce::String& deviceName, bool input) const;
    void moveInputs (int from, int to);
    void moveOutputs (int from, int to);
    void closeDevice (int slot);

    // Devices
    juce::AudioDeviceManager typeFactory;
    juce::OwnedArray<juce::AudioIODeviceType> deviceTypes;
    juce::StringArray scannedTypes;
    std::array<std::unique_ptr<Interface>, bridge::maxInterfaces> interfaces;
    std::unique_ptr<ComputerCallback> computerCallback;
    std::unique_ptr<juce::AudioIODevice> computerDevice;
    juce::String computerDeviceName;
    std::atomic<bool> computerRunning { false };

    // Clock bridges: cross[source][consumer interface]; created on demand, freed at shutdown
    std::array<std::array<std::atomic<DriftFifo*>, bridge::maxInterfaces>, bridge::numSources> cross;
    juce::OwnedArray<DriftFifo> crossStore;       // message thread only
    std::vector<float> returnScratchL, returnScratchR;

    // NDI
    NdiSendHub sendHub;
    NdiReceiver receiver;
    NdiFinder finder;

    // Message-thread configuration
    std::array<bridge::SourceConfig, bridge::numSources> sourceConfigs;
    std::array<bridge::BusConfig, bridge::numBuses> busConfigs;
    juce::String returnSourceName;
    double returnBufferMs = 60.0, computerBufferMs = 30.0, crossBufferMs = 25.0;

    // Audio-thread parameters
    std::array<std::atomic<float>, bridge::numSources> sourceGain;
    std::array<std::atomic<float>, bridge::numOutChannels> outGain;
    std::array<std::array<std::atomic<float>, bridge::numOutChannels>, bridge::numSources> routeGain;
    std::array<std::atomic<bool>, bridge::numBuses> busStereo;

    std::array<bridge::Meter, bridge::numSources> sourceMeters;
    std::array<bridge::Meter, bridge::numOutChannels> outputMeters;
    std::atomic<int> structureVersion { 0 };
    std::atomic<float> computerLoad { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

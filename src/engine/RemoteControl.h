#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

class AudioEngine;

/** OSC remote control over UDP, made for Bitfocus Companion's "Generic: OSC" module.

    Commands (no arguments needed; <i> = interface number, <n> = input number, both from 1):
        /bridge/in/<i>/<n>/cough/on        cough mute on (silent to vMix)
        /bridge/in/<i>/<n>/cough/off       cough mute off
        /bridge/in/<i>/<n>/cough/toggle
        /bridge/in/<i>/<n>/cough  <int|float|bool>   1 = mute, 0 = unmute (no argument = toggle)
        /bridge/computer/...               the same for computer audio
        /bridge/refresh                    send every value again

    Feedback, sent to every address a command came from and to the configured target:
        /bridge/in/<i>/<n>/live       int  1 = reaching vMix right now: NDI send on, cough off,
                                           device running and at least one NDI receiver connected
        /bridge/in/<i>/<n>/cough      int  1 = cough mute on
        /bridge/in/<i>/<n>/receivers  int  NDI receivers connected to this source
        /bridge/in/<i>/<n>/name       string
    Values go out when they change, and all of them every 5 seconds.

    Networking runs on its own thread; process() does the rest on the message thread.
*/
class RemoteControl : private juce::Thread
{
public:
    static constexpr int defaultPort = 9500;

    explicit RemoteControl (AudioEngine&);
    ~RemoteControl() override;

    struct Config
    {
        bool enabled = true;
        int port = defaultPort;
        juce::String feedbackTo;   // "host:port", comma-separated for several; learned from the first command if empty
    };

    /** Message thread. Re-opens the socket when the port or the on/off state changes. */
    void setConfig (const Config&);
    const Config& getConfig() const noexcept { return config; }

    /** Message thread, ~30 times a second: applies received commands, then sends feedback.
        Returns true if a command changed a source (refresh the UI and save). */
    bool process();

    /** True once after process() learned the feedback address (save the settings). */
    bool takeConfigChanged() noexcept { return std::exchange (configChanged, false); }

    // ---- status for the UI
    bool isListening() const noexcept { return listening.load(); }
    juce::String getError() const { return error; }
    juce::String getLastCommand() const { return lastCommand; }
    juce::String getLastCommandFrom() const { return lastCommandFrom; }
    juce::uint32 getLastCommandTime() const noexcept { return lastCommandMs; }
    /** Where feedback goes ("127.0.0.1:9501, ..."), empty if nobody yet, and when it last went. */
    juce::String getFeedbackTargets() const { return feedbackTargets; }
    juce::uint32 getLastFeedbackTime() const noexcept { return lastFeedbackMs; }

    /** Base OSC address of a source ("/bridge/in/1/3", "/bridge/computer"); empty if it has none. */
    static juce::String addressFor (int source);

    struct SourceState
    {
        bool sendEnabled = false, cough = false, running = false, live = false;
        int receivers = 0;
    };
    static SourceState stateOf (AudioEngine&, int source);

    // ---- a minimal OSC codec (public for the self-test)
    struct Arg
    {
        char type = 0;
        juce::int64 i = 0;
        double f = 0.0;
        juce::String s;
    };

    struct Message
    {
        juce::String address;
        std::vector<Arg> args;
    };

    /** Parses a packet (message or bundle). Returns false if it isn't OSC. */
    static bool parse (const void* data, size_t size, std::vector<Message>& out);
    static juce::MemoryBlock encode (const juce::String& address, int value);
    static juce::MemoryBlock encode (const juce::String& address, const juce::String& value);

private:
    struct Incoming { Message message; juce::String ip; int port = 0; };
    struct Outgoing { juce::String host; int port = 0; juce::MemoryBlock data; };
    struct Peer { juce::String host; int port = 0; };
    struct Sent { int live = -1, cough = -1, receivers = -1; juce::String name; };

    void run() override;
    void openSocket();
    void closeSocket();
    bool handle (const Incoming&);
    int sourceForAddress (const juce::String& address, juce::String& rest) const;
    std::vector<Peer> destinations() const;
    void sendFeedback();

    AudioEngine& engine;
    Config config;

    std::unique_ptr<juce::DatagramSocket> socket;
    std::atomic<bool> listening { false };
    juce::String error;

    std::mutex queueLock;
    std::vector<Incoming> incoming;   // network thread -> message thread
    std::vector<Outgoing> outgoing;   // message thread -> network thread

    // ---- message thread
    std::vector<Peer> peers;
    std::map<juce::String, Sent> sent;
    juce::uint32 lastFullMs = 0;
    bool needFull = true, configChanged = false;
    juce::String lastCommand, lastCommandFrom;
    juce::uint32 lastCommandMs = 0;
    juce::String feedbackTargets;
    juce::uint32 lastFeedbackMs = 0;

    JUCE_DECLARE_NON_COPYABLE (RemoteControl)
};

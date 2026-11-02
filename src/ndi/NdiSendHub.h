#pragma once

#include "dsp/StereoFifo.h"

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>

/** Owns every outgoing NDI audio source.

    Audio callbacks push samples into a slot's lock-free FIFO; a single worker
    thread creates/renames/destroys the NDI senders and ships the audio out in
    fixed 10 ms frames. Nothing NDI-related ever runs on an audio thread.
*/
class NdiSendHub : private juce::Thread
{
public:
    static constexpr int maxSlots = 72;

    struct Slot
    {
        // ---- written by the message thread
        std::atomic<bool> enabled { false };
        std::atomic<bool> stereo { true };   // false = 1-channel mono source
        std::atomic<bool> muted { false };

        // ---- written by the producing audio thread
        std::atomic<double> sampleRate { 48000.0 };

        // ---- written by the worker thread (for the UI)
        std::atomic<int> connections { 0 };
        std::atomic<bool> live { false };

        StereoFifo fifo;

        /** Audio thread: hand over one block. */
        void push (const float* l, const float* r, int numSamples, double rate) noexcept
        {
            if (! enabled.load (std::memory_order_relaxed))
                return;

            sampleRate.store (rate, std::memory_order_relaxed);
            fifo.push (l, r, numSamples, muted.load (std::memory_order_relaxed) ? 0.0f : 1.0f);
        }

    private:
        friend class NdiSendHub;
        mutable juce::SpinLock nameLock;
        juce::String name;
        std::atomic<int> nameVersion { 0 };
    };

    NdiSendHub();
    ~NdiSendHub() override;

    void start();
    void stop();

    Slot& getSlot (int index) noexcept { return slots[(size_t) index]; }

    /** Estimated bytes put on the network so far: payload x receivers (for the network glance). */
    juce::uint64 getBytesSent() const noexcept { return bytesSent.load(); }

    /** Message thread: set the NDI source name (the machine name is added by NDI). */
    void setName (int index, const juce::String& newName);
    juce::String getName (int index) const;

private:
    void run() override;

    std::array<Slot, maxSlots> slots;
    std::atomic<juce::uint64> bytesSent { 0 };
};

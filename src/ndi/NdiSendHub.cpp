#include "NdiSendHub.h"
#include "NdiLibrary.h"

#include <vector>

NdiSendHub::NdiSendHub() : juce::Thread ("NDI send")
{
    for (auto& s : slots)
        s.fifo.prepare (1 << 15); // ~680 ms at 48 kHz
}

NdiSendHub::~NdiSendHub()
{
    stop();
}

void NdiSendHub::start()
{
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::highest);
}

void NdiSendHub::stop()
{
    stopThread (2000);
}

void NdiSendHub::setName (int index, const juce::String& newName)
{
    auto& s = slots[(size_t) index];
    {
        const juce::SpinLock::ScopedLockType sl (s.nameLock);
        if (s.name == newName)
            return;
        s.name = newName;
    }
    s.nameVersion.fetch_add (1);
}

juce::String NdiSendHub::getName (int index) const
{
    auto& s = slots[(size_t) index];
    const juce::SpinLock::ScopedLockType sl (s.nameLock);
    return s.name;
}

void NdiSendHub::run()
{
    struct WorkerState
    {
        NDIlib_send_instance_t instance = nullptr;
        int builtVersion = -1;
        int pollCounter = 0;
    };

    std::array<WorkerState, maxSlots> state {};
    std::vector<float> planar;

    auto& ndi = ndi::lib();

    auto destroy = [&] (int i)
    {
        if (state[(size_t) i].instance != nullptr)
            ndi->send_destroy (state[(size_t) i].instance);

        state[(size_t) i].instance = nullptr;
        slots[(size_t) i].live = false;
        slots[(size_t) i].connections = 0;
    };

    while (! threadShouldExit())
    {
        for (int i = 0; i < maxSlots; ++i)
        {
            auto& slot = slots[(size_t) i];
            auto& st = state[(size_t) i];

            const bool want = ndi.isLoaded() && slot.enabled.load();

            if (! want)
            {
                if (st.instance != nullptr)
                    destroy (i);

                slot.fifo.discard();
                continue;
            }

            if (st.instance == nullptr || st.builtVersion != slot.nameVersion.load())
            {
                destroy (i);

                const auto version = slot.nameVersion.load();
                const auto name = getName (i);

                NDIlib_send_create_t create;
                create.p_ndi_name = name.toRawUTF8();
                create.p_groups = nullptr;
                create.clock_video = false;
                create.clock_audio = false; // the audio interface is the clock

                st.instance = ndi->send_create (&create);
                st.builtVersion = version;
                slot.live = st.instance != nullptr;
                slot.fifo.discard(); // start fresh, no stale backlog
            }

            if (st.instance == nullptr)
                continue;

            const double rate = slot.sampleRate.load();
            const int frame = juce::jmax (64, juce::roundToInt (rate / 100.0)); // 10 ms

            // If we fell far behind (e.g. the machine stalled), drop the backlog
            // to keep latency low instead of sending stale audio.
            if (slot.fifo.available() > frame * 25)
                slot.fifo.discard (slot.fifo.available() - frame * 2);

            const int channels = slot.stereo.load() ? 2 : 1;

            while (slot.fifo.available() >= frame)
            {
                planar.resize ((size_t) (frame * 2));
                float* l = planar.data();
                float* r = planar.data() + frame;
                slot.fifo.read (l, r, frame);

                NDIlib_audio_frame_v3_t f;
                f.sample_rate = juce::roundToInt (rate);
                f.no_channels = channels;
                f.no_samples = frame;
                f.timecode = NDIlib_send_timecode_synthesize;
                f.FourCC = NDIlib_FourCC_audio_type_FLTP;
                f.p_data = reinterpret_cast<uint8_t*> (planar.data());
                f.channel_stride_in_bytes = frame * (int) sizeof (float);
                f.p_metadata = nullptr;

                ndi->send_send_audio_v3 (st.instance, &f);
            }

            if (++st.pollCounter >= 250) // ~0.5 s
            {
                st.pollCounter = 0;
                slot.connections = ndi->send_get_no_connections (st.instance, 0);
            }
        }

        wait (2);
    }

    for (int i = 0; i < maxSlots; ++i)
        destroy (i);
}

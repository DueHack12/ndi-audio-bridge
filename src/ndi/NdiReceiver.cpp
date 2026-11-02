#include "NdiReceiver.h"
#include "NdiLibrary.h"

NdiReceiver::NdiReceiver (juce::String receiverName)
    : juce::Thread ("NDI receive"), recvName (std::move (receiverName))
{
}

NdiReceiver::~NdiReceiver()
{
    stop();
}

void NdiReceiver::start()
{
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::highest);
}

void NdiReceiver::stop()
{
    stopThread (2000);
}

void NdiReceiver::setSource (const juce::String& fullNdiName)
{
    {
        const juce::SpinLock::ScopedLockType sl (sourceLock);
        if (sourceName == fullNdiName)
            return;
        sourceName = fullNdiName;
    }
    sourceVersion.fetch_add (1);
}

juce::String NdiReceiver::getSource() const
{
    const juce::SpinLock::ScopedLockType sl (sourceLock);
    return sourceName;
}

bool NdiReceiver::isReceiving() const noexcept
{
    const auto last = lastAudioMs.load();
    return last != 0 && juce::Time::getMillisecondCounter() - last < 750;
}

void NdiReceiver::setChannelMap (int leftChannel, int rightChannel) noexcept
{
    leftChan = juce::jmax (0, leftChannel);
    rightChan = juce::jmax (0, rightChannel);
}

void NdiReceiver::run()
{
    auto& ndi = ndi::lib();
    NDIlib_recv_instance_t recv = nullptr;
    int builtVersion = -1;
    int pollCounter = 0;

    auto destroy = [&]
    {
        if (recv != nullptr)
            ndi->recv_destroy (recv);
        recv = nullptr;
        connected = false;
        lastAudioMs = 0;
        sourceChannels = 0;
        sourceRate = 0;
    };

    while (! threadShouldExit())
    {
        if (! ndi.isLoaded())
        {
            wait (250);
            continue;
        }

        if (builtVersion != sourceVersion.load())
        {
            destroy();
            builtVersion = sourceVersion.load();
            if (onReset)
                onReset();

            const auto name = getSource();

            if (name.isNotEmpty())
            {
                NDIlib_recv_create_v3_t create;
                create.source_to_connect_to.p_ndi_name = name.toRawUTF8();
                create.source_to_connect_to.p_url_address = nullptr; // NDI finds it by name
                create.color_format = NDIlib_recv_color_format_fastest;
                create.bandwidth = NDIlib_recv_bandwidth_audio_only;
                create.allow_video_fields = false;
                create.p_ndi_recv_name = recvName.toRawUTF8();

                recv = ndi->recv_create_v3 (&create);
            }
        }

        if (recv == nullptr)
        {
            wait (100);
            continue;
        }

        NDIlib_audio_frame_v3_t audio;
        const auto type = ndi->recv_capture_v3 (recv, nullptr, &audio, nullptr, 50);

        if (type == NDIlib_frame_type_audio)
        {
            if (audio.FourCC == NDIlib_FourCC_audio_type_FLTP && audio.no_channels > 0 && audio.no_samples > 0)
            {
                const int stride = audio.channel_stride_in_bytes;
                auto channelPtr = [&] (int ch) -> const float*
                {
                    ch = juce::jlimit (0, audio.no_channels - 1, ch);
                    return reinterpret_cast<const float*> (audio.p_data + (size_t) ch * (size_t) stride);
                };

                // A mono source plays in both ears.
                const float* l = channelPtr (leftChan.load());
                const float* r = audio.no_channels > 1 ? channelPtr (rightChan.load()) : l;

                if (onAudio)
                    onAudio (l, r, audio.no_samples, (double) audio.sample_rate);

                bytesReceived.fetch_add ((juce::uint64) audio.no_samples * (juce::uint64) audio.no_channels * sizeof (float));

                sourceChannels = audio.no_channels;
                sourceRate = audio.sample_rate;
                lastAudioMs = juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter());
            }

            ndi->recv_free_audio_v3 (recv, &audio);
        }

        if (++pollCounter >= 10)
        {
            pollCounter = 0;
            connected = ndi->recv_get_no_connections (recv) > 0;
        }
    }

    destroy();
}

#include "NdiFinder.h"
#include "NdiLibrary.h"

NdiFinder::NdiFinder() : juce::Thread ("NDI find") {}

NdiFinder::~NdiFinder()
{
    stop();
}

void NdiFinder::start()
{
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void NdiFinder::stop()
{
    stopThread (3000);
}

juce::StringArray NdiFinder::getSources() const
{
    const juce::ScopedLock sl (lock);
    return sources;
}

void NdiFinder::run()
{
    auto& ndi = ndi::lib();
    NDIlib_find_instance_t finder = nullptr;

    while (! threadShouldExit())
    {
        if (! ndi.isLoaded())
        {
            wait (500);
            continue;
        }

        if (finder == nullptr)
        {
            // Extra discovery IPs / groups come from the machine-wide NDI config
            // (set them with NDI Access Manager from NDI Tools).
            NDIlib_find_create_t create;
            create.show_local_sources = true;
            create.p_groups = nullptr;
            create.p_extra_ips = nullptr;
            finder = ndi->find_create_v2 (&create);

            if (finder == nullptr)
            {
                wait (1000);
                continue;
            }
        }

        ndi->find_wait_for_sources (finder, 500);

        uint32_t count = 0;
        const auto* list = ndi->find_get_current_sources (finder, &count);

        juce::StringArray found;
        for (uint32_t i = 0; i < count; ++i)
            if (list[i].p_ndi_name != nullptr)
                found.add (juce::String::fromUTF8 (list[i].p_ndi_name));

        found.sortNatural();

        {
            const juce::ScopedLock sl (lock);
            if (found != sources)
            {
                sources = found;
                changeCount.fetch_add (1);
            }
        }
    }

    if (finder != nullptr)
        ndi->find_destroy (finder);
}

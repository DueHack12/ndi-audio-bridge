#pragma once

#include <juce_core/juce_core.h>
#include <Processing.NDI.Lib.h>

namespace ndi
{
/** Loads the NDI runtime (libndi / Processing.NDI.Lib.x64.dll) at runtime.

    Nothing from NDI is linked at build time; the user installs the free NDI
    Runtime or NDI Tools. Set NDI_AUDIO_BRIDGE_NDI_LIB to a full path to force
    a specific library file.
*/
class Library
{
public:
    static Library& get();

    /** Tries to load the runtime once; later calls return the cached result. */
    bool load();

    bool isLoaded() const noexcept { return api != nullptr; }

    /** Only valid when isLoaded(). */
    const NDIlib_v6* operator->() const noexcept { return api; }

    juce::String getStatusText() const;
    juce::String getLoadedPath() const { return loadedPath; }

    /** Call once at shutdown after every sender/receiver/finder is destroyed. */
    void shutdown();

private:
    Library() = default;

    juce::CriticalSection lock;
    juce::DynamicLibrary dynLib;
    const NDIlib_v6* api = nullptr;
    bool attempted = false;
    juce::String loadedPath, error;
};

inline Library& lib() { return Library::get(); }
} // namespace ndi

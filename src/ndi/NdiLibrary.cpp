#include "NdiLibrary.h"

namespace ndi
{
namespace
{
juce::String getEnv (const char* name)
{
    return juce::SystemStats::getEnvironmentVariable (name, {});
}

juce::StringArray candidatePaths()
{
    juce::StringArray paths;

    auto addDir = [&paths] (const juce::String& dir)
    {
        if (dir.isNotEmpty())
            paths.addIfNotAlreadyThere (juce::File (dir).getChildFile (NDILIB_LIBRARY_NAME).getFullPathName());
    };

    if (auto forced = getEnv ("NDI_AUDIO_BRIDGE_NDI_LIB"); forced.isNotEmpty())
        paths.add (forced);

    addDir (getEnv (NDILIB_REDIST_FOLDER)); // NDI_RUNTIME_DIR_V6
    addDir (getEnv ("NDI_RUNTIME_DIR_V5"));

    // Next to the executable (lets you drop the runtime beside the app).
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    addDir (exe.getParentDirectory().getFullPathName());

   #if JUCE_MAC
    addDir (exe.getParentDirectory().getParentDirectory().getChildFile ("Frameworks").getFullPathName());
    paths.add ("/usr/local/lib/libndi.dylib");                         // NDI Runtime / NDI Tools
    paths.add ("/opt/homebrew/lib/libndi.dylib");
    paths.add ("/Library/NDI SDK for Apple/lib/macOS/libndi.dylib");   // NDI SDK
    paths.add ("/Library/NDI Advanced SDK for Apple/lib/macOS/libndi_advanced.dylib");
   #elif JUCE_WINDOWS
    paths.add ("C:\\Program Files\\NDI\\NDI 6 Runtime\\v6\\" NDILIB_LIBRARY_NAME);
    paths.add ("C:\\Program Files\\NDI\\NDI 6 Tools\\Runtime\\" NDILIB_LIBRARY_NAME);
    paths.add ("C:\\Program Files\\NDI\\NDI 6 SDK\\Bin\\x64\\" NDILIB_LIBRARY_NAME);
   #elif JUCE_LINUX
    paths.add ("libndi.so.6");
    paths.add ("libndi.so.5");
    paths.add ("libndi.so");
   #endif

    paths.addIfNotAlreadyThere (NDILIB_LIBRARY_NAME); // default OS search path
    return paths;
}
} // namespace

Library& Library::get()
{
    static Library instance;
    return instance;
}

bool Library::load()
{
    const juce::ScopedLock sl (lock);

    if (attempted)
        return api != nullptr;

    attempted = true;
    using LoadFn = const NDIlib_v6* (*) (void);

    for (auto& path : candidatePaths())
    {
        // Skip absolute paths that do not exist to avoid noisy loader errors.
        if (juce::File::isAbsolutePath (path) && ! juce::File (path).existsAsFile())
            continue;

        if (! dynLib.open (path))
            continue;

        auto loadFn = reinterpret_cast<LoadFn> (dynLib.getFunction ("NDIlib_v6_load"));
        if (loadFn == nullptr)
            loadFn = reinterpret_cast<LoadFn> (dynLib.getFunction ("NDIlib_v5_load"));

        if (loadFn != nullptr)
        {
            if (auto* candidate = loadFn())
            {
                if (candidate->initialize())
                {
                    api = candidate;
                    loadedPath = path;
                    return true;
                }

                error = "NDI runtime found but this CPU is not supported";
            }
        }

        dynLib.close();
    }

    if (error.isEmpty())
    {
        const juce::String url (NDILIB_REDIST_URL);
        error = "NDI runtime not found. Install NDI Tools or the NDI Runtime" + (url.isNotEmpty() ? " (" + url + ")" : juce::String());
    }

    return false;
}

juce::String Library::getStatusText() const
{
    if (api != nullptr)
    {
        juce::String v = api->version != nullptr ? juce::String::fromUTF8 (api->version()) : juce::String ("NDI");
        return v;
    }

    return error.isNotEmpty() ? error : juce::String ("NDI not loaded");
}

void Library::shutdown()
{
    const juce::ScopedLock sl (lock);

    if (api != nullptr)
        api->destroy();

    api = nullptr;
    dynLib.close();
}
} // namespace ndi

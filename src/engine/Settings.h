#pragma once

#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;

/** Saves/restores everything to a settings file:
      macOS:   ~/Library/Application Support/NDI Audio Bridge/NDI Audio Bridge.settings
      Windows: %APPDATA%\NDI Audio Bridge\NDI Audio Bridge.settings

    Sources and mixes are stored by device name ("US-4x4HR#0"), so they find their
    interface again even if interfaces are plugged in a different order.
*/
namespace Settings
{
juce::PropertiesFile& getFile();

/** Restores interfaces, names, levels, routing, the return source etc. into the engine.
    Call before AudioEngine::startup(). Settings from v0.1/v0.2 are migrated. */
void restore (AudioEngine&);

juce::String getSavedComputerDevice();
void save (AudioEngine&);

bool isDarkTheme();
void setDarkTheme (bool dark);
} // namespace Settings

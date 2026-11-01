#pragma once

#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;

/** Saves/restores everything to a settings file:
      macOS:   ~/Library/Application Support/NDI Audio Bridge/NDI Audio Bridge.settings
      Windows: %APPDATA%\NDI Audio Bridge\NDI Audio Bridge.settings
*/
namespace Settings
{
juce::PropertiesFile& getFile();

/** The saved interface (TASCAM) device setup, or null on first run. */
std::unique_ptr<juce::XmlElement> getSavedInterfaceState();

/** Restores names, gains, matrix, return source etc. into the engine's configs.
    Call before AudioEngine::startup(). */
void restoreConfig (AudioEngine&);

juce::String getSavedComputerDevice();

void save (AudioEngine&);
} // namespace Settings

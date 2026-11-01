#include "Settings.h"
#include "AudioEngine.h"

using namespace bridge;

namespace Settings
{
juce::PropertiesFile& getFile()
{
    static std::unique_ptr<juce::PropertiesFile> file = []
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "NDI Audio Bridge";
        o.folderName = "NDI Audio Bridge";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return std::make_unique<juce::PropertiesFile> (o);
    }();

    return *file;
}

std::unique_ptr<juce::XmlElement> getSavedInterfaceState()
{
    return getFile().getXmlValue ("interface");
}

juce::String getSavedComputerDevice()
{
    if (auto xml = getFile().getXmlValue ("bridge"))
        return xml->getStringAttribute ("computerDevice");

    return {};
}

void restoreConfig (AudioEngine& engine)
{
    auto xml = getFile().getXmlValue ("bridge");
    if (xml == nullptr)
        return;

    for (auto* e : xml->getChildWithTagNameIterator ("SOURCE"))
    {
        const int i = e->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (i, numSources))
            continue;

        auto& c = engine.getSourceConfig (i);
        c.name = e->getStringAttribute ("name", c.name);
        c.gainDb = (float) e->getDoubleAttribute ("gainDb", c.gainDb);
        c.sendEnabled = e->getBoolAttribute ("send", c.sendEnabled);
        c.sendStereo = e->getBoolAttribute ("stereo", c.sendStereo);
        c.sendMuted = e->getBoolAttribute ("muted", c.sendMuted);
    }

    for (auto* e : xml->getChildWithTagNameIterator ("BUS"))
    {
        const int b = e->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (b, maxBuses))
            continue;

        auto& bc = engine.getBusConfig (b);
        bc.name = e->getStringAttribute ("name", bc.name);
        bc.gainDb = (float) e->getDoubleAttribute ("gainDb", bc.gainDb);

        const auto routes = e->getStringAttribute ("routes");
        if (routes.length() == numSources)
            for (int s = 0; s < numSources; ++s)
                bc.routes[s] = routes[s] == '1';
    }

    engine.setReturnSource (xml->getStringAttribute ("returnSource"));
    engine.setReturnBufferMs (xml->getDoubleAttribute ("returnBufferMs", engine.getReturnBufferMs()));
    engine.setComputerBufferMs (xml->getDoubleAttribute ("computerBufferMs", engine.getComputerBufferMs()));
}

void save (AudioEngine& engine)
{
    juce::XmlElement xml ("BRIDGE");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("returnSource", engine.getReturnSource());
    xml.setAttribute ("returnBufferMs", engine.getReturnBufferMs());
    xml.setAttribute ("computerBufferMs", engine.getComputerBufferMs());
    xml.setAttribute ("computerDevice", engine.getComputerAudioDeviceName());

    for (int i = 0; i < numSources; ++i)
    {
        auto& c = engine.getSourceConfig (i);
        auto* e = xml.createNewChildElement ("SOURCE");
        e->setAttribute ("index", i);
        e->setAttribute ("name", c.name);
        e->setAttribute ("gainDb", c.gainDb);
        e->setAttribute ("send", c.sendEnabled);
        e->setAttribute ("stereo", c.sendStereo);
        e->setAttribute ("muted", c.sendMuted);
    }

    for (int b = 0; b < maxBuses; ++b)
    {
        auto& bc = engine.getBusConfig (b);
        auto* e = xml.createNewChildElement ("BUS");
        e->setAttribute ("index", b);
        e->setAttribute ("name", bc.name);
        e->setAttribute ("gainDb", bc.gainDb);

        juce::String routes;
        for (int s = 0; s < numSources; ++s)
            routes << (bc.routes[s] ? "1" : "0");
        e->setAttribute ("routes", routes);
    }

    auto& file = getFile();
    file.setValue ("bridge", &xml);

    if (auto state = engine.getInterfaceManager().createStateXml())
        file.setValue ("interface", state.get());

    file.saveIfNeeded();
}
} // namespace Settings

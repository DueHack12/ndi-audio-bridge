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
        bc.nameRight = e->getStringAttribute ("nameRight", bc.nameRight);
        bc.gainDb = (float) e->getDoubleAttribute ("gainDb", bc.gainDb);
        bc.gainDbRight = (float) e->getDoubleAttribute ("gainDbRight", bc.gainDb);
        bc.stereo = e->getBoolAttribute ("stereo", true);

        auto readRoutes = [&] (const juce::String& attr, bool* dest)
        {
            const auto text = e->getStringAttribute (attr);
            if (text.length() != numSources)
                return false;
            for (int s = 0; s < numSources; ++s)
                dest[s] = text[s] == '1';
            return true;
        };

        // v0.1.0 stored one route per pair ("routes"): apply it to both sides.
        if (! readRoutes ("routesL", bc.routes[0]) && readRoutes ("routes", bc.routes[0]))
            std::copy (std::begin (bc.routes[0]), std::end (bc.routes[0]), std::begin (bc.routes[1]));
        else
            readRoutes ("routesR", bc.routes[1]);
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
        e->setAttribute ("stereo", bc.stereo);
        e->setAttribute ("nameRight", bc.nameRight);
        e->setAttribute ("gainDbRight", bc.gainDbRight);

        for (int side = 0; side < 2; ++side)
        {
            juce::String routes;
            for (int s = 0; s < numSources; ++s)
                routes << (bc.routes[side][s] ? "1" : "0");
            e->setAttribute (side == 0 ? "routesL" : "routesR", routes);
        }
    }

    auto& file = getFile();
    file.setValue ("bridge", &xml);

    if (auto state = engine.getInterfaceManager().createStateXml())
        file.setValue ("interface", state.get());

    file.saveIfNeeded();
}
} // namespace Settings

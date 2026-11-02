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

bool isDarkTheme()               { return getFile().getBoolValue ("darkTheme", false); }
void setDarkTheme (bool dark)    { getFile().setValue ("darkTheme", dark); getFile().saveIfNeeded(); }

juce::String getSavedComputerDevice()
{
    if (auto xml = getFile().getXmlValue ("bridge"))
        return xml->getStringAttribute ("computerDevice");
    return {};
}

static void readSource (const juce::XmlElement& e, SourceConfig& c)
{
    c.name = e.getStringAttribute ("name", c.name);
    c.gainDb = (float) e.getDoubleAttribute ("gainDb", c.gainDb);
    c.sendEnabled = e.getBoolAttribute ("send", c.sendEnabled);
    c.sendStereo = e.getBoolAttribute ("stereo", c.sendStereo);
    c.sendMuted = e.getBoolAttribute ("muted", c.sendMuted);
}

static void readBusBasics (const juce::XmlElement& e, BusConfig& bc)
{
    bc.name = e.getStringAttribute ("name", bc.name);
    bc.nameRight = e.getStringAttribute ("nameRight", bc.nameRight);
    bc.gainDb = (float) e.getDoubleAttribute ("gainDb", bc.gainDb);
    bc.gainDbRight = (float) e.getDoubleAttribute ("gainDbRight", bc.gainDb);
    bc.stereo = e.getBoolAttribute ("stereo", true);
}

/** v0.1/v0.2 stored one interface (AudioDeviceManager state) and fixed indices:
    sources 0..15 = inputs, 16 = computer, 17 = return; buses 0..7 = output pairs. */
static void migrateV2 (AudioEngine& engine, const juce::XmlElement& xml)
{
    if (auto dev = getFile().getXmlValue ("interface"))
    {
        InterfaceConfig cfg;
        cfg.typeName = dev->getStringAttribute ("deviceType");
        cfg.outputName = dev->getStringAttribute ("audioOutputDeviceName");
        cfg.inputName = dev->getStringAttribute ("audioInputDeviceName");
        cfg.sampleRate = dev->getDoubleAttribute ("audioDeviceRate", 48000.0);
        cfg.bufferSize = dev->getIntAttribute ("audioDeviceBufferSize", 128);
        if (cfg.getKey().isNotEmpty())
            engine.setInterfaceConfig (0, cfg);
    }

    auto mapOld = [] (int old) { return old < 16 ? sourceIndex (0, old) : (old == 16 ? computerSource : returnSource); };

    for (auto* e : xml.getChildWithTagNameIterator ("SOURCE"))
    {
        const int i = e->getIntAttribute ("index", -1);
        if (juce::isPositiveAndBelow (i, 18))
            readSource (*e, engine.getSourceConfig (mapOld (i)));
    }

    for (auto* e : xml.getChildWithTagNameIterator ("BUS"))
    {
        const int b = e->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (b, maxPairsPerInterface))
            continue;

        auto& bc = engine.getBusConfig (busIndex (0, b));
        readBusBasics (*e, bc);

        auto routesOf = [&] (const juce::String& attr) { auto t = e->getStringAttribute (attr); return t.length() == 18 ? t : juce::String(); };
        auto left = routesOf ("routesL"), right = routesOf ("routesR");
        if (left.isEmpty())
            left = right = routesOf ("routes");

        auto levels = [&] (const juce::String& attr) { return juce::StringArray::fromTokens (e->getStringAttribute (attr), ",", {}); };
        const auto levelsL = levels ("levelsL"), levelsR = levels ("levelsR");

        for (int old = 0; old < 18; ++old)
        {
            const int s = mapOld (old);
            if (left.isNotEmpty())  bc.routes[0][s] = left[old] == '1';
            if (right.isNotEmpty()) bc.routes[1][s] = right[old] == '1';
            if (levelsL.size() == 18) bc.levelsDb[0][s] = levelsL[old].getFloatValue();
            if (levelsR.size() == 18) bc.levelsDb[1][s] = levelsR[old].getFloatValue();
        }
    }
}

void restore (AudioEngine& engine)
{
    auto xml = getFile().getXmlValue ("bridge");
    if (xml == nullptr)
        return;

    if (xml->getIntAttribute ("version", 1) < 3)
    {
        migrateV2 (engine, *xml);
    }
    else
    {
        for (auto* e : xml->getChildWithTagNameIterator ("INTERFACE"))
        {
            const int slot = e->getIntAttribute ("slot", -1);
            if (! juce::isPositiveAndBelow (slot, maxInterfaces))
                continue;

            InterfaceConfig cfg;
            cfg.typeName = e->getStringAttribute ("type");
            cfg.inputName = e->getStringAttribute ("input");
            cfg.outputName = e->getStringAttribute ("output");
            cfg.sampleRate = e->getDoubleAttribute ("rate", 48000.0);
            cfg.bufferSize = e->getIntAttribute ("buffer", 128);
            cfg.maxIns = e->getIntAttribute ("ins", 0);
            cfg.maxOuts = e->getIntAttribute ("outs", 0);
            engine.setInterfaceConfig (slot, cfg);
        }

        for (auto* e : xml->getChildWithTagNameIterator ("SOURCE"))
        {
            const int s = engine.findSourceByKey (e->getStringAttribute ("key"));
            if (s >= 0)
                readSource (*e, engine.getSourceConfig (s));
        }

        for (auto* e : xml->getChildWithTagNameIterator ("BUS"))
        {
            const int b = engine.findBusByKey (e->getStringAttribute ("key"));
            if (b < 0)
                continue;

            auto& bc = engine.getBusConfig (b);
            readBusBasics (*e, bc);

            for (auto& side : bc.routes)
                std::fill (std::begin (side), std::end (side), false);

            for (auto* r : e->getChildWithTagNameIterator ("ROUTE"))
            {
                const int s = engine.findSourceByKey (r->getStringAttribute ("src"));
                const int side = r->getIntAttribute ("side", 0);
                if (s < 0 || ! juce::isPositiveAndBelow (side, 2))
                    continue;

                bc.routes[side][s] = true;
                bc.levelsDb[side][s] = (float) r->getDoubleAttribute ("db", 0.0);
            }
        }
    }

    engine.setReturnSource (xml->getStringAttribute ("returnSource"));
    engine.setReturnBufferMs (xml->getDoubleAttribute ("returnBufferMs", engine.getReturnBufferMs()));
    engine.setComputerBufferMs (xml->getDoubleAttribute ("computerBufferMs", engine.getComputerBufferMs()));
    engine.setCrossBufferMs (xml->getDoubleAttribute ("crossBufferMs", engine.getCrossBufferMs()));
}

void save (AudioEngine& engine)
{
    juce::XmlElement xml ("BRIDGE");
    xml.setAttribute ("version", 3);
    xml.setAttribute ("returnSource", engine.getReturnSource());
    xml.setAttribute ("returnBufferMs", engine.getReturnBufferMs());
    xml.setAttribute ("computerBufferMs", engine.getComputerBufferMs());
    xml.setAttribute ("crossBufferMs", engine.getCrossBufferMs());
    xml.setAttribute ("computerDevice", engine.getComputerAudioDeviceName());

    auto saveSource = [&] (int s)
    {
        auto& c = engine.getSourceConfig (s);
        auto* e = xml.createNewChildElement ("SOURCE");
        e->setAttribute ("key", engine.getSourceKey (s));
        e->setAttribute ("name", c.name);
        e->setAttribute ("gainDb", c.gainDb);
        e->setAttribute ("send", c.sendEnabled);
        e->setAttribute ("stereo", c.sendStereo);
        e->setAttribute ("muted", c.sendMuted);
    };

    for (int slot = 0; slot < maxInterfaces; ++slot)
    {
        if (! engine.isInterfaceUsed (slot))
            continue;

        const auto& cfg = engine.getInterfaceConfig (slot);
        auto* e = xml.createNewChildElement ("INTERFACE");
        e->setAttribute ("slot", slot);
        e->setAttribute ("type", cfg.typeName);
        e->setAttribute ("input", cfg.inputName);
        e->setAttribute ("output", cfg.outputName);
        e->setAttribute ("rate", cfg.sampleRate);
        e->setAttribute ("buffer", cfg.bufferSize);
        e->setAttribute ("ins", cfg.maxIns);
        e->setAttribute ("outs", cfg.maxOuts);

        for (int ch = 0; ch < maxInsPerInterface; ++ch)
            saveSource (sourceIndex (slot, ch));
    }

    saveSource (computerSource);
    saveSource (returnSource);

    for (int b = 0; b < numBuses; ++b)
    {
        if (! engine.isInterfaceUsed (interfaceOfBus (b)))
            continue;

        auto& bc = engine.getBusConfig (b);
        auto* e = xml.createNewChildElement ("BUS");
        e->setAttribute ("key", engine.getBusKey (b));
        e->setAttribute ("stereo", bc.stereo);
        e->setAttribute ("name", bc.name);
        e->setAttribute ("nameRight", bc.nameRight);
        e->setAttribute ("gainDb", bc.gainDb);
        e->setAttribute ("gainDbRight", bc.gainDbRight);

        for (int side = 0; side < 2; ++side)
        {
            for (int s = 0; s < numSources; ++s)
            {
                if (! bc.routes[side][s])
                    continue;

                const bool deviceSource = interfaceOfSource (s) >= 0;
                if (deviceSource && ! engine.isInterfaceUsed (interfaceOfSource (s)))
                    continue;

                auto* r = e->createNewChildElement ("ROUTE");
                r->setAttribute ("src", engine.getSourceKey (s));
                r->setAttribute ("side", side);
                r->setAttribute ("db", bc.levelsDb[side][s]);
            }
        }
    }

    auto& file = getFile();
    file.setValue ("bridge", &xml);
    file.removeValue ("interface"); // v0.2's single-interface state is now part of "bridge"
    file.saveIfNeeded();
}
} // namespace Settings

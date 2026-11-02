#pragma once

#include "engine/AudioEngine.h"
#include <vector>

namespace ui
{
/** A headphone mix as the UI sees it: a stereo pair (side -1) or one output of a
    pair used as 2x mono (side 0 or 1). */
struct MixRef
{
    int bus = 0;
    int side = -1;

    bool isStereo() const noexcept { return side < 0; }
    bool operator== (const MixRef& o) const noexcept { return bus == o.bus && side == o.side; }
};

/** Meter peaks read once per UI tick, so several views can show the same meter. */
struct Levels
{
    std::array<float, bridge::numSources> source {};
    std::array<float, bridge::numOutChannels> output {};

    void read (AudioEngine& e)
    {
        for (int s = 0; s < bridge::numSources; ++s)
            source[(size_t) s] = e.getSourceMeter (s).readAndReset();
        for (int b = 0; b < bridge::numBuses; ++b)
            for (int side = 0; side < 2; ++side)
                output[(size_t) (b * 2 + side)] = e.getOutputMeter (b, side).readAndReset();
    }

    float mix (MixRef m) const
    {
        if (! m.isStereo())
            return output[(size_t) (m.bus * 2 + m.side)];
        return juce::jmax (output[(size_t) (m.bus * 2)], output[(size_t) (m.bus * 2 + 1)]);
    }
};

/** Helpers that translate "a source in a mix" into the engine's per-side bus config. */
struct RoutingModel
{
    static std::vector<int> listSources (AudioEngine& e)
    {
        std::vector<int> out;
        for (int s = 0; s < bridge::computerSource; ++s)
            if (e.sourceExists (s))
                out.push_back (s);
        out.push_back (bridge::computerSource);
        out.push_back (bridge::returnSource);
        return out;
    }

    static std::vector<MixRef> listMixes (AudioEngine& e)
    {
        std::vector<MixRef> out;
        for (int b = 0; b < bridge::numBuses; ++b)
        {
            if (! e.busExists (b))
                continue;

            if (e.busHasSecondOutput (b) && e.getBusConfig (b).stereo)
                out.push_back ({ b, -1 });
            else if (e.busHasSecondOutput (b))
            {
                out.push_back ({ b, 0 });
                out.push_back ({ b, 1 });
            }
            else
                out.push_back ({ b, 0 });
        }
        return out;
    }

    static bool sideOn (AudioEngine& e, MixRef m, int s, int side) { return e.getBusConfig (m.bus).routes[side][s]; }

    static bool isRouted (AudioEngine& e, MixRef m, int s)
    {
        return m.isStereo() ? (sideOn (e, m, s, 0) || sideOn (e, m, s, 1)) : sideOn (e, m, s, m.side);
    }

    static float level (AudioEngine& e, MixRef m, int s)
    {
        auto& bc = e.getBusConfig (m.bus);
        if (! m.isStereo())
            return bc.levelsDb[m.side][s];
        return bc.routes[0][s] || ! bc.routes[1][s] ? bc.levelsDb[0][s] : bc.levelsDb[1][s];
    }

    static void setRouted (AudioEngine& e, MixRef m, int s, bool on)
    {
        auto& bc = e.getBusConfig (m.bus);
        if (m.isStereo())
        {
            bc.routes[0][s] = bc.routes[1][s] = on;
            if (on)
                bc.levelsDb[1][s] = bc.levelsDb[0][s];
        }
        else
            bc.routes[m.side][s] = on;
        e.applyBusConfig (m.bus);
    }

    static void setSide (AudioEngine& e, MixRef m, int s, int side, bool on)
    {
        auto& bc = e.getBusConfig (m.bus);
        bc.routes[side][s] = on;
        bc.levelsDb[side][s] = bc.levelsDb[1 - side][s];
        e.applyBusConfig (m.bus);
    }

    static void setLevel (AudioEngine& e, MixRef m, int s, float db)
    {
        auto& bc = e.getBusConfig (m.bus);
        if (m.isStereo())
            bc.levelsDb[0][s] = bc.levelsDb[1][s] = db;
        else
            bc.levelsDb[m.side][s] = db;
        e.applyBusConfig (m.bus);
    }

    static juce::String name (AudioEngine& e, MixRef m)
    {
        auto& bc = e.getBusConfig (m.bus);
        return m.side == 1 ? bc.nameRight : bc.name;
    }

    static void setName (AudioEngine& e, MixRef m, const juce::String& n)
    {
        auto& bc = e.getBusConfig (m.bus);
        (m.side == 1 ? bc.nameRight : bc.name) = n;
    }

    static juce::String outputLabel (AudioEngine& e, MixRef m)
    {
        return m.isStereo() ? e.getBusLabel (m.bus) : e.getOutputLabel (m.bus, m.side);
    }

    static juce::String shortDevice (AudioEngine& e, int slot)
    {
        auto n = e.getInterfaceName (slot);
        return n.length() > 16 ? n.substring (0, 15) + juce::String::fromUTF8 ("\xe2\x80\xa6") : n;
    }

    static juce::String subtitle (AudioEngine& e, MixRef m)
    {
        return shortDevice (e, bridge::interfaceOfBus (m.bus)) + " " + outputLabel (e, m) + (m.isStereo() ? juce::String::fromUTF8 (" · stereo") : juce::String::fromUTF8 (" · mono"));
    }

    static float master (AudioEngine& e, MixRef m)
    {
        auto& bc = e.getBusConfig (m.bus);
        return m.side == 1 ? bc.gainDbRight : bc.gainDb;
    }

    static void setMaster (AudioEngine& e, MixRef m, float db)
    {
        auto& bc = e.getBusConfig (m.bus);
        (m.side == 1 ? bc.gainDbRight : bc.gainDb) = db;
        e.applyBusConfig (m.bus);
    }

    static juce::String sourceTag (int s)
    {
        if (s == bridge::computerSource) return "PC";
        if (s == bridge::returnSource)   return "NDI";
        return "IN " + juce::String (bridge::channelOfSource (s) + 1);
    }
};
} // namespace ui

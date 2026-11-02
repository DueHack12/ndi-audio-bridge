#pragma once

#include <cstdint>
#include <map>
#include <string>

/** Samples whole-computer CPU, this app's CPU, and network throughput.
    Call update() about once a second from the message thread. Values are -1
    when the platform doesn't report them. */
class SystemMonitor
{
public:
    struct Snapshot
    {
        float appCpu = -1.0f;         // % of the whole machine used by this app
        float systemCpu = -1.0f;      // % of the whole machine in use
        double netRxBitsPerSec = -1.0;
        double netTxBitsPerSec = -1.0;
        double linkBitsPerSec = -1.0; // speed of the busiest network interface
        std::string linkName;
    };

    void update();
    const Snapshot& get() const noexcept { return snap; }

    struct NetCounter
    {
        std::string name;
        uint64_t rx = 0, tx = 0;
        double linkBitsPerSec = -1.0;
        bool counters32Bit = false;
    };

private:
    Snapshot snap;
    bool primed = false;
    uint64_t lastIdle = 0, lastTotal = 0;
    double lastProcSeconds = -1.0, lastWall = 0.0;
    std::map<std::string, NetCounter> lastNet;
};

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

    /** This computer's IPv4 addresses on adapters that are up: no loopback, link-local or
        multicast. Best guess for "the LAN address" first: private ranges on real adapters
        before VPN / virtual adapters (Hyper-V, WSL, Radmin, Tailscale, Docker...). */
    struct LocalAddress
    {
        std::string ip, adapter;   // adapter: "Ethernet", "Wi-Fi" (Windows), "en0" (macOS)
        bool isVirtual = false;
    };
    static std::vector<LocalAddress> listAddresses();

    // ---- the pieces of listAddresses() (public for the self-test)
    static bool isUsableIPv4 (const unsigned char* bytes);
    static LocalAddress describeAddress (const unsigned char* bytes, const std::string& adapter, const std::string& description);
    static void sortByPreference (std::vector<LocalAddress>&);

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

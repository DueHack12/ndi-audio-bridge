// Platform code only, no JUCE headers, so the OS headers stay out of JUCE's way.
#include "SystemMonitor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <thread>
#include <vector>

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <winsock2.h>
 #include <ws2ipdef.h>   // must come before iphlpapi.h, or the MIB_IF_ROW2 / GetIfTable2 API is hidden
 #include <windows.h>
 #include <iphlpapi.h>
 #include <netioapi.h>
 #pragma comment (lib, "iphlpapi.lib")
#elif defined (__APPLE__)
 #include <mach/mach.h>
 #include <sys/socket.h>
 #include <sys/resource.h>
 #include <ifaddrs.h>
 #include <net/if.h>
 #include <netinet/in.h>
#else
 #include <sys/resource.h>
 #include <sys/socket.h>
 #include <ifaddrs.h>
 #include <net/if.h>
 #include <netinet/in.h>
 #include <fstream>
 #include <sstream>
#endif

namespace
{
double wallSeconds()
{
    using namespace std::chrono;
    return duration<double> (steady_clock::now().time_since_epoch()).count();
}

#if defined (_WIN32)
uint64_t toU64 (const FILETIME& ft) { return ((uint64_t) ft.dwHighDateTime << 32) | ft.dwLowDateTime; }

bool readCpuTimes (uint64_t& idle, uint64_t& total)
{
    FILETIME i, k, u;
    if (! GetSystemTimes (&i, &k, &u))
        return false;
    idle = toU64 (i);
    total = toU64 (k) + toU64 (u); // kernel time includes idle time
    return true;
}

double processCpuSeconds()
{
    FILETIME c, e, k, u;
    if (! GetProcessTimes (GetCurrentProcess(), &c, &e, &k, &u))
        return -1.0;
    return (double) (toU64 (k) + toU64 (u)) * 1.0e-7;
}

std::vector<SystemMonitor::NetCounter> readNet()
{
    std::vector<SystemMonitor::NetCounter> out;
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2 (&table) != NO_ERROR || table == nullptr)
        return out;

    for (ULONG i = 0; i < table->NumEntries; ++i)
    {
        const auto& row = table->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp
            || ! row.InterfaceAndOperStatusFlags.HardwareInterface)
            continue;

        SystemMonitor::NetCounter c;
        char name[256] = {};
        WideCharToMultiByte (CP_UTF8, 0, row.Alias, -1, name, (int) sizeof (name) - 1, nullptr, nullptr);
        c.name = name;
        c.rx = row.InOctets;
        c.tx = row.OutOctets;
        c.linkBitsPerSec = (double) row.TransmitLinkSpeed;
        out.push_back (c);
    }

    FreeMibTable (table);
    return out;
}

#elif defined (__APPLE__)
bool readCpuTimes (uint64_t& idle, uint64_t& total)
{
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics (mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t) &info, &count) != KERN_SUCCESS)
        return false;

    idle = info.cpu_ticks[CPU_STATE_IDLE];
    total = (uint64_t) info.cpu_ticks[CPU_STATE_USER] + info.cpu_ticks[CPU_STATE_SYSTEM]
            + info.cpu_ticks[CPU_STATE_NICE] + info.cpu_ticks[CPU_STATE_IDLE];
    return true;
}

std::vector<SystemMonitor::NetCounter> readNet()
{
    std::vector<SystemMonitor::NetCounter> out;
    ifaddrs* list = nullptr;
    if (getifaddrs (&list) != 0)
        return out;

    for (auto* a = list; a != nullptr; a = a->ifa_next)
    {
        if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_LINK || a->ifa_data == nullptr)
            continue;
        if ((a->ifa_flags & IFF_LOOPBACK) != 0 || (a->ifa_flags & IFF_UP) == 0)
            continue;

        const auto* d = static_cast<const if_data*> (a->ifa_data);
        SystemMonitor::NetCounter c;
        c.name = a->ifa_name;
        c.rx = d->ifi_ibytes;
        c.tx = d->ifi_obytes;
        c.linkBitsPerSec = d->ifi_baudrate > 0 ? (double) d->ifi_baudrate : -1.0;
        c.counters32Bit = true;
        out.push_back (c);
    }

    freeifaddrs (list);
    return out;
}

#else
bool readCpuTimes (uint64_t& idle, uint64_t& total)
{
    std::ifstream f ("/proc/stat");
    std::string cpu;
    uint64_t v[8] = {};
    if (! (f >> cpu >> v[0] >> v[1] >> v[2] >> v[3] >> v[4] >> v[5] >> v[6] >> v[7]) || cpu != "cpu")
        return false;

    idle = v[3] + v[4];
    total = 0;
    for (auto x : v) total += x;
    return true;
}

std::vector<SystemMonitor::NetCounter> readNet()
{
    std::vector<SystemMonitor::NetCounter> out;
    std::ifstream f ("/proc/net/dev");
    std::string line;

    for (int i = 0; std::getline (f, line); ++i)
    {
        if (i < 2) continue; // headers
        const auto colon = line.find (':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr (0, colon);
        name.erase (0, name.find_first_not_of (' '));
        if (name == "lo") continue;

        std::istringstream rest (line.substr (colon + 1));
        uint64_t fields[16] = {};
        for (auto& x : fields) rest >> x;

        SystemMonitor::NetCounter c;
        c.name = name;
        c.rx = fields[0];
        c.tx = fields[8];

        std::ifstream speed ("/sys/class/net/" + name + "/speed");
        long mbps = -1;
        if (speed >> mbps && mbps > 0)
            c.linkBitsPerSec = (double) mbps * 1.0e6;

        out.push_back (c);
    }

    return out;
}
#endif

//==============================================================================
// Local IPv4 addresses
SystemMonitor::LocalAddress makeAddress (const unsigned char* b, const std::string& adapter, const std::string& description)
{
    SystemMonitor::LocalAddress a;
    a.ip = std::to_string (b[0]) + "." + std::to_string (b[1]) + "." + std::to_string (b[2]) + "." + std::to_string (b[3]);
    a.adapter = adapter;

    std::string text = adapter + " " + description;
    std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    for (const char* v : { "vethernet", "virtual", "hyper-v", "vmware", "vmnet", "virtualbox", "vbox", "wsl", "docker", "veth",
                           "virbr", "br-", "bridge", "radmin", "hamachi", "zerotier", "tailscale", "wireguard", "vpn", "tap-",
                           "tun", "utun", "ppp", "awdl", "llw", "loopback", "npcap" })
        if (text.find (v) != std::string::npos)
            a.isVirtual = true;

    return a;
}

bool usableAddress (const unsigned char* b)
{
    return b[0] != 0 && b[0] != 127 && b[0] < 224         // "this network", loopback, multicast & reserved
        && ! (b[0] == 169 && b[1] == 254);                // link-local (no DHCP)
}

int addressRank (const SystemMonitor::LocalAddress& a)
{
    int b0 = 0, b1 = 0;
    std::sscanf (a.ip.c_str(), "%d.%d", &b0, &b1);
    int rank = 3;                                                    // public, or a VPN's range (e.g. Radmin's 26.x)
    if (b0 == 192 && b1 == 168)                rank = 0;
    else if (b0 == 10)                         rank = 1;
    else if (b0 == 172 && b1 >= 16 && b1 < 32) rank = 2;
    else if (b0 == 100 && b1 >= 64 && b1 < 128) rank = 4;          // carrier-grade NAT / Tailscale
    return rank + (a.isVirtual ? 10 : 0);
}

#if defined (_WIN32)
std::string narrow (const wchar_t* w)
{
    if (w == nullptr || *w == 0)
        return {};
    const int n = WideCharToMultiByte (CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s ((size_t) std::max (0, n - 1), '\0');
    if (n > 1)
        WideCharToMultiByte (CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::vector<SystemMonitor::LocalAddress> readAddresses()
{
    std::vector<SystemMonitor::LocalAddress> out;
    std::vector<unsigned char> buffer;
    ULONG size = 32 * 1024, result = ERROR_BUFFER_OVERFLOW;

    for (int attempt = 0; attempt < 4 && result == ERROR_BUFFER_OVERFLOW; ++attempt)
    {
        buffer.resize (size);
        result = GetAdaptersAddresses (AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                       nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*> (buffer.data()), &size);
    }

    if (result != NO_ERROR)
        return out;

    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*> (buffer.data()); a != nullptr; a = a->Next)
    {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;

        const auto name = narrow (a->FriendlyName), description = narrow (a->Description);
        for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
        {
            const auto* sa = u->Address.lpSockaddr;
            if (sa == nullptr || sa->sa_family != AF_INET)
                continue;

            const auto* b = reinterpret_cast<const unsigned char*> (&reinterpret_cast<const sockaddr_in*> (sa)->sin_addr);
            if (usableAddress (b))
                out.push_back (makeAddress (b, name, description));
        }
    }

    return out;
}
#else
std::vector<SystemMonitor::LocalAddress> readAddresses()
{
    std::vector<SystemMonitor::LocalAddress> out;
    ifaddrs* list = nullptr;
    if (getifaddrs (&list) != 0)
        return out;

    for (auto* a = list; a != nullptr; a = a->ifa_next)
    {
        if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_INET)
            continue;
        if ((a->ifa_flags & IFF_LOOPBACK) != 0 || (a->ifa_flags & IFF_UP) == 0)
            continue;

        const auto* b = reinterpret_cast<const unsigned char*> (&reinterpret_cast<const sockaddr_in*> (a->ifa_addr)->sin_addr);
        if (usableAddress (b))
            out.push_back (makeAddress (b, a->ifa_name != nullptr ? a->ifa_name : "", {}));
    }

    freeifaddrs (list);
    return out;
}
#endif

#if ! defined (_WIN32)
double processCpuSeconds()
{
    rusage u {};
    if (getrusage (RUSAGE_SELF, &u) != 0)
        return -1.0;
    return (double) u.ru_utime.tv_sec + u.ru_utime.tv_usec * 1.0e-6
         + (double) u.ru_stime.tv_sec + u.ru_stime.tv_usec * 1.0e-6;
}
#endif
} // namespace

std::vector<SystemMonitor::LocalAddress> SystemMonitor::listAddresses()
{
    auto list = readAddresses();
    sortByPreference (list);
    return list;
}

bool SystemMonitor::isUsableIPv4 (const unsigned char* bytes)  { return usableAddress (bytes); }

SystemMonitor::LocalAddress SystemMonitor::describeAddress (const unsigned char* bytes, const std::string& adapter, const std::string& description)
{
    return makeAddress (bytes, adapter, description);
}

void SystemMonitor::sortByPreference (std::vector<LocalAddress>& list)
{
    std::stable_sort (list.begin(), list.end(), [] (const LocalAddress& a, const LocalAddress& b) { return addressRank (a) < addressRank (b); });
}

void SystemMonitor::update()
{
    const double now = wallSeconds();
    const double dt = now - lastWall;

    uint64_t idle = 0, total = 0;
    const bool haveCpu = readCpuTimes (idle, total);
    const double proc = processCpuSeconds();
    const auto net = readNet();

    if (primed && dt > 0.05)
    {
        if (haveCpu && total > lastTotal)
        {
            const double dTotal = (double) (total - lastTotal);
            const double dIdle = (double) (idle - lastIdle);
            snap.systemCpu = (float) (100.0 * (1.0 - dIdle / dTotal));
        }

        if (proc >= 0.0 && lastProcSeconds >= 0.0)
        {
            const unsigned cores = std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 1u;
            snap.appCpu = (float) (100.0 * (proc - lastProcSeconds) / dt / cores);
        }

        double rx = 0, tx = 0, busiest = -1;
        bool any = false;
        for (const auto& c : net)
        {
            auto it = lastNet.find (c.name);
            if (it == lastNet.end())
                continue;

            auto delta = [&c] (uint64_t nowV, uint64_t before) -> double
            {
                if (c.counters32Bit)
                    return (double) (uint32_t) ((uint32_t) nowV - (uint32_t) before);
                return nowV >= before ? (double) (nowV - before) : 0.0;
            };

            const double dRx = delta (c.rx, it->second.rx), dTx = delta (c.tx, it->second.tx);
            rx += dRx;
            tx += dTx;
            any = true;

            if (dRx + dTx > busiest)
            {
                busiest = dRx + dTx;
                snap.linkBitsPerSec = c.linkBitsPerSec;
                snap.linkName = c.name;
            }
        }

        if (any)
        {
            snap.netRxBitsPerSec = rx * 8.0 / dt;
            snap.netTxBitsPerSec = tx * 8.0 / dt;
        }
    }

    lastIdle = idle;
    lastTotal = total;
    lastProcSeconds = proc;
    lastWall = now;
    lastNet.clear();
    for (const auto& c : net)
        lastNet[c.name] = c;
    primed = true;
}

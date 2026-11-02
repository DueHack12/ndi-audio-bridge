#include "RemoteControl.h"
#include "AudioEngine.h"

#include <cstring>

using namespace bridge;

namespace
{
constexpr const char* prefix = "/bridge";
constexpr juce::uint32 fullRefreshMs = 5000;
constexpr size_t maxPeers = 8, maxQueued = 256;

//==============================================================================
// OSC reading: everything is big-endian and 4-byte aligned.
struct Reader
{
    const juce::uint8* data;
    size_t size, pos = 0;

    bool string (juce::String& out)
    {
        size_t end = pos;
        while (end < size && data[end] != 0)
            ++end;
        if (end >= size)
            return false;

        out = juce::String::fromUTF8 (reinterpret_cast<const char*> (data + pos), (int) (end - pos));
        pos += ((end - pos) + 4) & ~(size_t) 3; // the terminator plus padding
        return pos <= size;
    }

    bool skip (size_t n)  { pos += n; return pos <= size; }

    bool int32 (juce::int32& v)
    {
        if (pos + 4 > size) return false;
        v = (juce::int32) juce::ByteOrder::bigEndianInt (data + pos);
        pos += 4;
        return true;
    }

    bool int64 (juce::int64& v)
    {
        if (pos + 8 > size) return false;
        v = (juce::int64) juce::ByteOrder::bigEndianInt64 (data + pos);
        pos += 8;
        return true;
    }
};

bool parseMessage (const juce::uint8* data, size_t size, std::vector<RemoteControl::Message>& out)
{
    Reader r { data, size };
    RemoteControl::Message m;
    if (! r.string (m.address) || ! m.address.startsWithChar ('/'))
        return false;

    juce::String tags;
    if (r.pos < size && (! r.string (tags) || ! tags.startsWithChar (',')))
        return false;

    for (int t = 1; t < tags.length(); ++t)
    {
        RemoteControl::Arg a;
        a.type = (char) tags[t];
        juce::int32 v32 = 0;
        juce::int64 v64 = 0;
        bool ok = true;

        switch (a.type)
        {
            case 'i': case 'c': case 'r': case 'm':
                ok = r.int32 (v32); a.i = v32; a.f = v32; break;
            case 'f':
            {
                ok = r.int32 (v32);
                float f;
                std::memcpy (&f, &v32, sizeof (f));
                a.f = f; a.i = (juce::int64) f;
                break;
            }
            case 'h': case 't':
                ok = r.int64 (v64); a.i = v64; a.f = (double) v64; break;
            case 'd':
            {
                ok = r.int64 (v64);
                double d;
                std::memcpy (&d, &v64, sizeof (d));
                a.f = d; a.i = (juce::int64) d;
                break;
            }
            case 's': case 'S':
                ok = r.string (a.s); break;
            case 'b':
                ok = r.int32 (v32) && v32 >= 0 && r.skip (((size_t) v32 + 3) & ~(size_t) 3); break;
            case 'T': a.i = 1; a.f = 1.0; break;
            case 'F': case 'N': case 'I': case '[': case ']': break;
            default: ok = false; break; // unknown type: its size is unknown, stop here
        }

        if (! ok)
            break;
        m.args.push_back (std::move (a));
    }

    out.push_back (std::move (m));
    return true;
}

bool parsePacket (const juce::uint8* data, size_t size, std::vector<RemoteControl::Message>& out, int depth)
{
    if (size >= 16 && std::memcmp (data, "#bundle", 8) == 0)
    {
        if (depth > 4)
            return false;

        Reader r { data, size, 16 }; // "#bundle\0" + time tag
        bool any = false;
        juce::int32 n = 0;
        while (r.int32 (n) && n > 0 && r.pos + (size_t) n <= size)
        {
            any = parsePacket (data + r.pos, (size_t) n, out, depth + 1) || any;
            r.pos += (size_t) n;
        }
        return any;
    }

    return size >= 4 && parseMessage (data, size, out);
}

void writeString (juce::MemoryOutputStream& o, const juce::String& s)
{
    const auto* utf8 = s.toRawUTF8();
    const auto n = std::strlen (utf8);
    o.write (utf8, n);
    for (size_t pad = 4 - (n % 4); pad > 0; --pad)
        o.writeByte (0);
}

bool truthy (const RemoteControl::Arg& a)
{
    switch (a.type)
    {
        case 'f': case 'd': return a.f >= 0.5;
        case 's': case 'S': return a.s.trim() == "1" || a.s.trim().equalsIgnoreCase ("on") || a.s.trim().equalsIgnoreCase ("true");
        default:            return a.i != 0;
    }
}

bool isNumber (const juce::String& s)
{
    return s.isNotEmpty() && s.containsOnly ("0123456789");
}
} // namespace

//==============================================================================
bool RemoteControl::parse (const void* data, size_t size, std::vector<Message>& out)
{
    return parsePacket (static_cast<const juce::uint8*> (data), size, out, 0);
}

juce::MemoryBlock RemoteControl::encode (const juce::String& address, int value)
{
    juce::MemoryOutputStream o;
    writeString (o, address);
    writeString (o, ",i");
    o.writeIntBigEndian (value);
    return o.getMemoryBlock();
}

juce::MemoryBlock RemoteControl::encode (const juce::String& address, const juce::String& value)
{
    juce::MemoryOutputStream o;
    writeString (o, address);
    writeString (o, ",s");
    writeString (o, value);
    return o.getMemoryBlock();
}

//==============================================================================
RemoteControl::RemoteControl (AudioEngine& e) : juce::Thread ("OSC control"), engine (e)
{
}

RemoteControl::~RemoteControl()
{
    closeSocket();
}

juce::String RemoteControl::addressFor (int source)
{
    if (source == computerSource)
        return juce::String (prefix) + "/computer";

    const int slot = interfaceOfSource (source);
    if (slot < 0)
        return {};

    return juce::String (prefix) + "/in/" + juce::String (slot + 1) + "/" + juce::String (channelOfSource (source) + 1);
}

RemoteControl::SourceState RemoteControl::stateOf (AudioEngine& e, int source)
{
    SourceState st;
    const auto& cfg = e.getSourceConfig (source);
    auto& slot = e.getSendHub().getSlot (source);

    st.sendEnabled = cfg.sendEnabled;
    st.cough = cfg.sendMuted;
    st.running = slot.live.load();
    st.receivers = st.running ? juce::jmax (0, slot.connections.load()) : 0;
    st.live = st.sendEnabled && ! st.cough && st.running && st.receivers > 0;
    return st;
}

//==============================================================================
void RemoteControl::setConfig (const Config& c)
{
    const bool reopen = c.enabled != config.enabled || c.port != config.port || socket == nullptr;
    const bool newTarget = c.feedbackTo != config.feedbackTo;
    config = c;
    config.port = juce::jlimit (1, 65535, config.port);

    if (reopen)
    {
        closeSocket();
        if (config.enabled)
            openSocket();
    }

    if (newTarget)
        needFull = true;
}

void RemoteControl::openSocket()
{
    error.clear();
    socket = std::make_unique<juce::DatagramSocket> (false);
    if (! socket->bindToPort (config.port))
    {
        socket.reset();
        error = "UDP port " + juce::String (config.port) + " is in use. Pick another port.";
        return;
    }

    listening = true;
    needFull = true;
    startThread();
}

void RemoteControl::closeSocket()
{
    signalThreadShouldExit();
    if (socket != nullptr)
        socket->shutdown();
    stopThread (2000);
    socket.reset();
    listening = false;

    std::lock_guard<std::mutex> g (queueLock);
    incoming.clear();
    outgoing.clear();
}

void RemoteControl::run()
{
    std::vector<char> buffer (65536);
    std::vector<Outgoing> toSend;
    std::vector<Message> messages;

    while (! threadShouldExit())
    {
        {
            std::lock_guard<std::mutex> g (queueLock);
            toSend.swap (outgoing);
        }
        for (auto& o : toSend)
            socket->write (o.host, o.port, o.data.getData(), (int) o.data.getSize());
        toSend.clear();

        const int ready = socket->waitUntilReady (true, 10);
        if (ready < 0)
        {
            wait (50);
            continue;
        }
        if (ready == 0)
            continue;

        juce::String ip;
        int port = 0;
        const int n = socket->read (buffer.data(), (int) buffer.size(), false, ip, port);
        if (n <= 0)
            continue; // e.g. Windows reports "port unreachable" from an earlier send here: ignore it

        messages.clear();
        if (! parse (buffer.data(), (size_t) n, messages))
            continue;

        std::lock_guard<std::mutex> g (queueLock);
        for (auto& m : messages)
            if (incoming.size() < maxQueued)
                incoming.push_back ({ std::move (m), ip, port });
    }
}

//==============================================================================
bool RemoteControl::process()
{
    if (! listening.load())
        return false;

    std::vector<Incoming> in;
    {
        std::lock_guard<std::mutex> g (queueLock);
        in.swap (incoming);
    }

    bool changed = false;
    for (auto& i : in)
        changed = handle (i) || changed;

    sendFeedback();
    return changed;
}

int RemoteControl::sourceForAddress (const juce::String& address, juce::String& rest) const
{
    auto parts = juce::StringArray::fromTokens (address.toLowerCase(), "/", "");
    parts.removeEmptyStrings();
    if (parts.size() < 2 || parts[0] != juce::String (prefix).substring (1))
        return -1;

    int s = -1, used = 0;
    if (parts[1] == "computer")
    {
        s = computerSource;
        used = 2;
    }
    else if (parts[1] == "in" && parts.size() >= 4 && isNumber (parts[2]) && isNumber (parts[3]))
    {
        const int slot = parts[2].getIntValue() - 1, ch = parts[3].getIntValue() - 1;
        if (! juce::isPositiveAndBelow (slot, maxInterfaces) || ! juce::isPositiveAndBelow (ch, maxInsPerInterface))
            return -1;
        s = sourceIndex (slot, ch);
        used = 4;
    }

    if (s < 0 || ! engine.sourceExists (s))
        return -1;

    parts.removeRange (0, used);
    rest = parts.joinIntoString ("/");
    return s;
}

bool RemoteControl::handle (const Incoming& in)
{
    // Whoever sends a command gets feedback (Companion replies from its "Source Port").
    bool known = false;
    for (auto& p : peers)
        known = known || (p.host == in.ip && p.port == in.port);
    if (! known && in.port > 0)
    {
        if (peers.size() >= maxPeers)
            peers.erase (peers.begin());
        peers.push_back ({ in.ip, in.port });
        needFull = true;
    }

    if (config.feedbackTo.isEmpty() && in.port > 0)
    {
        config.feedbackTo = in.ip + ":" + juce::String (in.port);
        configChanged = true;
    }

    lastCommand = in.message.address;
    lastCommandFrom = in.ip + ":" + juce::String (in.port);
    lastCommandMs = juce::Time::getMillisecondCounter();

    const auto address = in.message.address.toLowerCase();
    if (address == juce::String (prefix) + "/refresh")
    {
        needFull = true;
        return false;
    }

    juce::String rest;
    const int s = sourceForAddress (address, rest);
    if (s < 0)
        return false;

    auto& cfg = engine.getSourceConfig (s);
    bool cough = cfg.sendMuted;
    if (rest == "cough/on")             cough = true;
    else if (rest == "cough/off")       cough = false;
    else if (rest == "cough/toggle")    cough = ! cough;
    else if (rest == "cough")           cough = in.message.args.empty() ? ! cough : truthy (in.message.args.front());
    else                                return false;

    sent.erase (addressFor (s)); // always answer with this source's state, changed or not

    if (cough == cfg.sendMuted)
        return false;

    cfg.sendMuted = cough;
    engine.applySourceConfig (s);
    return true;
}

std::vector<RemoteControl::Peer> RemoteControl::destinations() const
{
    std::vector<Peer> out = peers;

    for (auto& t : juce::StringArray::fromTokens (config.feedbackTo, ", ;", ""))
    {
        const auto host = t.upToLastOccurrenceOf (":", false, false).trim();
        const int port = t.fromLastOccurrenceOf (":", false, false).getIntValue();
        if (host.isEmpty() || ! juce::isPositiveAndBelow (port - 1, 65535) || ! t.containsChar (':'))
            continue;

        bool dup = false;
        for (auto& p : out)
            dup = dup || (p.host == host && p.port == port);
        if (! dup)
            out.push_back ({ host, port });
    }

    return out;
}

void RemoteControl::sendFeedback()
{
    const auto dests = destinations();
    if (dests.empty())
        return;

    const auto now = juce::Time::getMillisecondCounter();
    const bool full = needFull || now - lastFullMs >= fullRefreshMs;
    if (full)
    {
        needFull = false;
        lastFullMs = now;
    }

    std::vector<juce::MemoryBlock> packets;
    std::map<juce::String, Sent> current;

    auto add = [&] (int s)
    {
        const auto base = addressFor (s);
        const auto st = stateOf (engine, s);
        auto& now_ = current[base];
        now_.live = st.live ? 1 : 0;
        now_.cough = st.cough ? 1 : 0;
        now_.receivers = st.receivers;
        now_.name = engine.getSourceConfig (s).name;

        const auto it = sent.find (base);
        const bool all = full || it == sent.end();
        if (all || it->second.live != now_.live)            packets.push_back (encode (base + "/live", now_.live));
        if (all || it->second.cough != now_.cough)          packets.push_back (encode (base + "/cough", now_.cough));
        if (all || it->second.receivers != now_.receivers)  packets.push_back (encode (base + "/receivers", now_.receivers));
        if (all || it->second.name != now_.name)            packets.push_back (encode (base + "/name", now_.name));
    };

    for (int s = 0; s < computerSource; ++s)
        if (engine.sourceExists (s))
            add (s);
    add (computerSource);

    // A source that went away (interface removed or unplugged) is no longer live.
    for (auto& [base, old] : sent)
        if (current.find (base) == current.end() && old.live != 0)
            packets.push_back (encode (base + "/live", 0));

    sent = std::move (current);

    if (packets.empty())
        return;

    std::lock_guard<std::mutex> g (queueLock);
    for (auto& d : dests)
        for (auto& p : packets)
            if (outgoing.size() < 4 * maxQueued)
                outgoing.push_back ({ d.host, d.port, p });
}

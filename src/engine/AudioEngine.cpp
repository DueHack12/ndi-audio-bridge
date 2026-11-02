#include "AudioEngine.h"
#include "ndi/NdiLibrary.h"

using namespace bridge;

namespace
{
constexpr int chunkSize = 1024;

float peakOf (const float* data, int n) noexcept
{
    auto range = juce::FloatVectorOperations::findMinAndMax (data, n);
    return juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
}

float dbToGain (float db) noexcept
{
    return db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
}

void smoothLoad (std::atomic<float>& load, juce::int64 startTicks, int numSamples, double rate) noexcept
{
    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const double budget = numSamples / juce::jmax (1.0, rate);
    const float now = (float) (elapsed / budget);
    const float prev = load.load (std::memory_order_relaxed);
    load.store (now > prev ? now : prev + 0.05f * (now - prev), std::memory_order_relaxed);
}
} // namespace

//==============================================================================
/** One audio interface: its device, its clock and its part of the routing. */
struct AudioEngine::Interface : juce::AudioIODeviceCallback
{
    Interface (AudioEngine& e, int s) : engine (e), slot (s) {}

    AudioEngine& engine;
    const int slot;

    // message thread
    InterfaceConfig config;
    bool used = false;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String status;

    // shared
    std::atomic<bool> running { false };
    std::atomic<int> numIns { 0 }, numOuts { 0 };
    int deviceIns = 0, deviceOuts = 0;          // what the hardware offers (message thread)
    std::atomic<double> rate { 48000.0 };
    std::atomic<float> load { 0.0f };
    std::atomic<juce::uint32> lastCallbackMs { 0 };

    // audio thread
    int numActiveIns = 0;
    std::array<int, 64> activeIn {};
    std::array<int, 64> outIndexForCh {};
    juce::AudioBuffer<float> inputScratch, crossScratch, mixScratch;
    std::array<bool, maxInsPerInterface> inputPresent {};
    std::array<bool, numSources> crossPresent {};

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        const double r = d->getCurrentSampleRate();
        rate = r;

        const auto ins = d->getActiveInputChannels();
        const auto outs = d->getActiveOutputChannels();
        numActiveIns = 0;
        outIndexForCh.fill (-1);
        int outIndex = 0;

        for (int ch = 0; ch < 64; ++ch)
        {
            if (ins[ch])
                activeIn[(size_t) numActiveIns++] = ch;
            if (outs[ch])
                outIndexForCh[(size_t) ch] = outIndex++;
        }

        numIns = juce::jlimit (0, maxInsPerInterface, ins.getHighestBit() + 1);
        numOuts = juce::jlimit (0, maxPairsPerInterface * 2, outs.getHighestBit() + 1);

        inputScratch.setSize (maxInsPerInterface, chunkSize);
        crossScratch.setSize (numSources * 2, chunkSize);
        mixScratch.setSize (1, chunkSize);

        for (int s = 0; s < numSources; ++s)
            if (auto* f = engine.cross[(size_t) s][(size_t) slot].load())
                f->setConsumerSampleRate (r);

        engine.bumpStructure();
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        const auto start = juce::Time::getHighResolutionTicks();

        for (int i = 0; i < numOut; ++i)
            if (out[i] != nullptr)
                juce::FloatVectorOperations::clear (out[i], numSamples);

        for (int offset = 0; offset < numSamples; offset += chunkSize)
            processChunk (in, numIn, out, numOut, offset, juce::jmin (chunkSize, numSamples - offset));

        smoothLoad (load, start, numSamples, rate.load (std::memory_order_relaxed));
        lastCallbackMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    }

    void processChunk (const float* const* in, int numIn, float* const* out, int numOut, int offset, int n) noexcept
    {
        const double r = rate.load (std::memory_order_relaxed);

        // 1. Inputs -> trim -> NDI send, and on to any other interface that mixes them
        inputPresent.fill (false);

        for (int a = 0; a < juce::jmin (numIn, numActiveIns); ++a)
        {
            const int ch = activeIn[(size_t) a];
            if (ch >= maxInsPerInterface || in[a] == nullptr)
                continue;

            const int s = sourceIndex (slot, ch);
            float* buf = inputScratch.getWritePointer (ch);
            juce::FloatVectorOperations::copyWithMultiply (buf, in[a] + offset, engine.sourceGain[(size_t) s].load(), n);
            engine.sourceMeters[(size_t) s].update (peakOf (buf, n));
            engine.sendHub.getSlot (s).push (buf, nullptr, n, r);
            engine.fanOut (s, buf, nullptr, n, r, slot);
            inputPresent[(size_t) ch] = true;
        }

        // 2. Audio arriving from other clocks (other interfaces, computer audio, NDI return)
        for (int s = 0; s < numSources; ++s)
        {
            crossPresent[(size_t) s] = false;
            if (interfaceOfSource (s) == slot)
                continue;

            if (auto* f = engine.cross[(size_t) s][(size_t) slot].load (std::memory_order_acquire))
            {
                f->pull (crossScratch.getWritePointer (s * 2), crossScratch.getWritePointer (s * 2 + 1), n);
                crossPresent[(size_t) s] = true;
            }
        }

        // 3. Every output channel is its own mix
        const int outs = numOuts.load (std::memory_order_relaxed);
        float* mix = mixScratch.getWritePointer (0);

        for (int ch = 0; ch < outs; ++ch)
        {
            const int idx = outIndexForCh[(size_t) ch];
            if (idx < 0 || idx >= numOut || out[idx] == nullptr)
                continue;

            const int pair = ch / 2, side = ch % 2;
            const int bus = busIndex (slot, pair);
            const int o = bus * 2 + side;
            const int rightCh = pair * 2 + 1;
            const bool hasRight = rightCh < outs && outIndexForCh[(size_t) rightCh] >= 0;
            const bool stereo = hasRight && engine.busStereo[(size_t) bus].load (std::memory_order_relaxed);

            juce::FloatVectorOperations::clear (mix, n);

            for (int s = 0; s < numSources; ++s)
            {
                const float g = engine.routeGain[(size_t) s][(size_t) o].load (std::memory_order_relaxed);
                if (g <= 0.0f)
                    continue;

                if (interfaceOfSource (s) == slot)
                {
                    const int c = channelOfSource (s);
                    if (inputPresent[(size_t) c])
                        juce::FloatVectorOperations::addWithMultiply (mix, inputScratch.getReadPointer (c), g, n);
                }
                else if (crossPresent[(size_t) s])
                {
                    const float* l = crossScratch.getReadPointer (s * 2);
                    const float* rr = crossScratch.getReadPointer (s * 2 + 1);

                    if (s >= computerSource && ! stereo)
                    {
                        // Stereo source into a mono mix: fold to mono.
                        juce::FloatVectorOperations::addWithMultiply (mix, l, g * 0.5f, n);
                        juce::FloatVectorOperations::addWithMultiply (mix, rr, g * 0.5f, n);
                    }
                    else
                    {
                        juce::FloatVectorOperations::addWithMultiply (mix, side == 0 ? l : rr, g, n);
                    }
                }
            }

            juce::FloatVectorOperations::multiply (mix, engine.outGain[(size_t) o].load(), n);
            engine.outputMeters[(size_t) o].update (peakOf (mix, n));
            juce::FloatVectorOperations::copy (out[idx] + offset, mix, n);
        }
    }
};

//==============================================================================
/** The virtual "computer audio" device (BlackHole-based on macOS, VB-CABLE on Windows). */
struct AudioEngine::ComputerCallback : juce::AudioIODeviceCallback
{
    explicit ComputerCallback (AudioEngine& e) : engine (e) {}

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        sampleRate = device->getCurrentSampleRate();
        scratchL.allocate (chunkSize, true);
        scratchR.allocate (chunkSize, true);
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        const auto start = juce::Time::getHighResolutionTicks();

        for (int i = 0; i < numOut; ++i)
            if (out[i] != nullptr)
                juce::FloatVectorOperations::clear (out[i], numSamples);

        if (numIn < 1 || in[0] == nullptr)
            return;

        const float* l = in[0];
        const float* r = (numIn > 1 && in[1] != nullptr) ? in[1] : in[0];
        const float gain = engine.sourceGain[(size_t) computerSource].load();

        for (int offset = 0; offset < numSamples; offset += chunkSize)
        {
            const int n = juce::jmin (chunkSize, numSamples - offset);
            juce::FloatVectorOperations::copyWithMultiply (scratchL.get(), l + offset, gain, n);
            juce::FloatVectorOperations::copyWithMultiply (scratchR.get(), r + offset, gain, n);
            engine.sourceMeters[(size_t) computerSource].update (juce::jmax (peakOf (scratchL.get(), n), peakOf (scratchR.get(), n)));
            engine.sendHub.getSlot (computerSource).push (scratchL.get(), scratchR.get(), n, sampleRate);
            engine.fanOut (computerSource, scratchL.get(), scratchR.get(), n, sampleRate, -1);
        }

        smoothLoad (engine.computerLoad, start, numSamples, sampleRate);
    }

    AudioEngine& engine;
    double sampleRate = 48000.0;
    juce::HeapBlock<float> scratchL, scratchR;
};

//==============================================================================
AudioEngine::AudioEngine()
{
    for (int i = 0; i < maxInterfaces; ++i)
        interfaces[(size_t) i] = std::make_unique<Interface> (*this, i);

    computerCallback = std::make_unique<ComputerCallback> (*this);

    for (int s = 0; s < numSources; ++s)
    {
        auto& c = sourceConfigs[(size_t) s];
        c.name = defaultSourceName (s);
        c.sendEnabled = (s != returnSource);
        sourceGain[(size_t) s].store (1.0f);

        for (auto& f : cross[(size_t) s])
            f.store (nullptr);
        for (auto& g : routeGain[(size_t) s])
            g.store (0.0f);
    }

    for (int b = 0; b < numBuses; ++b)
    {
        auto& bc = busConfigs[(size_t) b];
        bc.name = "Headphones " + juce::String (pairOfBus (b) + 1);
        bc.nameRight = bc.name + " R";
        bc.routes[0][returnSource] = bc.routes[1][returnSource] = true; // everyone hears the vMix return
        busStereo[(size_t) b].store (true);
    }

    for (auto& g : outGain)
        g.store (1.0f);

    receiver.onAudio = [this] (const float* l, const float* r, int n, double rate)
    {
        // Receive thread: apply the return's level, meter it, hand it to every interface.
        if ((int) returnScratchL.size() < n)
        {
            returnScratchL.resize ((size_t) n);
            returnScratchR.resize ((size_t) n);
        }

        const float g = sourceGain[(size_t) returnSource].load();
        juce::FloatVectorOperations::copyWithMultiply (returnScratchL.data(), l, g, n);
        juce::FloatVectorOperations::copyWithMultiply (returnScratchR.data(), r, g, n);
        sourceMeters[(size_t) returnSource].update (juce::jmax (peakOf (returnScratchL.data(), n), peakOf (returnScratchR.data(), n)));
        fanOut (returnSource, returnScratchL.data(), returnScratchR.data(), n, rate, -1);
    };

    receiver.onReset = [this]
    {
        for (auto& f : cross[(size_t) returnSource])
            if (auto* fifo = f.load())
                fifo->requestReset();
    };
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

juce::String AudioEngine::defaultSourceName (int source)
{
    if (source == computerSource) return "Computer Audio";
    if (source == returnSource)   return "vMix Return";

    const int slot = interfaceOfSource (source), ch = channelOfSource (source);
    return slot == 0 ? "Input " + juce::String (ch + 1)
                     : "Input " + juce::String (slot + 1) + "." + juce::String (ch + 1);
}

//==============================================================================
void AudioEngine::startup()
{
    ndi::lib().load();

    if (deviceTypes.isEmpty())
        typeFactory.createAudioDeviceTypes (deviceTypes);

    sendHub.start();
    receiver.start();
    finder.start();

    for (int i = 0; i < maxInterfaces; ++i)
        if (interfaces[(size_t) i]->used)
            openInterface (i);

    for (int s = 0; s < numSources; ++s)
        applySourceConfig (s);

    for (int b = 0; b < numBuses; ++b)
        applyBusConfig (b);

    receiver.setSource (returnSourceName);
}

void AudioEngine::shutdown()
{
    for (auto& it : interfaces)
    {
        if (it != nullptr && it->device != nullptr)
        {
            it->running = false;
            it->device->stop();
            it->device->close();
            it->device.reset();
        }
    }

    openComputerAudioDevice ({});

    finder.stop();
    receiver.stop();
    sendHub.stop();

    for (auto& row : cross)
        for (auto& f : row)
            f.store (nullptr);
    crossStore.clear();

    ndi::lib().shutdown();
}

//==============================================================================
juce::AudioIODeviceType* AudioEngine::findType (const juce::String& typeName)
{
    if (deviceTypes.isEmpty())
        typeFactory.createAudioDeviceTypes (deviceTypes);

    for (auto* t : deviceTypes)
        if (t->getTypeName() == typeName)
            return t;

    return nullptr;
}

juce::StringArray AudioEngine::getDeviceTypeNames()
{
    if (deviceTypes.isEmpty())
        typeFactory.createAudioDeviceTypes (deviceTypes);

    juce::StringArray names;
    for (auto* t : deviceTypes)
        names.add (t->getTypeName());
    return names;
}

juce::String AudioEngine::getDefaultDeviceTypeName()
{
    auto names = getDeviceTypeNames();
   #if JUCE_WINDOWS
    if (names.contains ("ASIO")) return "ASIO";
   #endif
    return names.isEmpty() ? juce::String() : names[0];
}

juce::StringArray AudioEngine::getDeviceNames (const juce::String& typeName, bool inputs, bool rescan)
{
    if (auto* t = findType (typeName))
    {
        if (rescan || ! scannedTypes.contains (typeName))
        {
            t->scanForDevices();
            scannedTypes.addIfNotAlreadyThere (typeName);
        }
        return t->getDeviceNames (inputs);
    }

    return {};
}

juce::Array<double> AudioEngine::getSampleRates (int slot) const
{
    auto& it = *interfaces[(size_t) slot];
    if (it.device != nullptr)
        return it.device->getAvailableSampleRates();
    return { 44100.0, 48000.0, 88200.0, 96000.0 };
}

juce::Array<int> AudioEngine::getBufferSizes (int slot) const
{
    auto& it = *interfaces[(size_t) slot];
    if (it.device != nullptr)
        return it.device->getAvailableBufferSizes();
    return { 32, 64, 128, 256, 512, 1024 };
}

bool AudioEngine::isInterfaceUsed (int slot) const noexcept      { return interfaces[(size_t) slot]->used; }
bool AudioEngine::isInterfaceRunning (int slot) const noexcept   { return interfaces[(size_t) slot]->running.load(); }
const InterfaceConfig& AudioEngine::getInterfaceConfig (int slot) const { return interfaces[(size_t) slot]->config; }
int AudioEngine::getNumInputs (int slot) const noexcept          { return interfaces[(size_t) slot]->numIns.load(); }
int AudioEngine::getNumOutputs (int slot) const noexcept         { return interfaces[(size_t) slot]->numOuts.load(); }
double AudioEngine::getInterfaceSampleRate (int slot) const noexcept { return interfaces[(size_t) slot]->rate.load(); }
int AudioEngine::getDeviceInputCount (int slot) const noexcept  { return interfaces[(size_t) slot]->deviceIns; }
int AudioEngine::getDeviceOutputCount (int slot) const noexcept { return interfaces[(size_t) slot]->deviceOuts; }

juce::String AudioEngine::getInterfaceName (int slot) const
{
    auto key = interfaces[(size_t) slot]->config.getKey();
    return key.isNotEmpty() ? key : "Interface " + juce::String (slot + 1);
}

juce::String AudioEngine::getInterfaceStatus (int slot) const
{
    auto& it = *interfaces[(size_t) slot];
    if (! it.used)
        return {};

    if (it.running.load() && it.device != nullptr)
    {
        const auto last = it.lastCallbackMs.load();
        if (last != 0 && juce::Time::getMillisecondCounter() - last > 1500)
            return "Not responding";

        return juce::String (it.device->getCurrentSampleRate() / 1000.0, 1) + juce::String::fromUTF8 (" kHz · ")
               + juce::String (it.device->getCurrentBufferSizeSamples()) + juce::String::fromUTF8 (" samples · ")
               + juce::String (it.numIns.load()) + " in / " + juce::String (it.numOuts.load()) + " out";
    }

    return it.status.isNotEmpty() ? it.status : juce::String ("Not connected");
}

void AudioEngine::setInterfaceConfig (int slot, const InterfaceConfig& cfg)
{
    auto& it = *interfaces[(size_t) slot];
    it.config = cfg;
    it.used = true;
    bumpStructure();
}

void AudioEngine::resetSlotDefaults (int slot)
{
    for (int ch = 0; ch < maxInsPerInterface; ++ch)
    {
        const int s = sourceIndex (slot, ch);
        sourceConfigs[(size_t) s] = {};
        sourceConfigs[(size_t) s].name = defaultSourceName (s);
    }

    for (int p = 0; p < maxPairsPerInterface; ++p)
    {
        auto& bc = busConfigs[(size_t) busIndex (slot, p)];
        bc = {};
        bc.name = "Headphones " + juce::String (p + 1);
        bc.nameRight = bc.name + " R";
        bc.routes[0][returnSource] = bc.routes[1][returnSource] = true;
    }
}

int AudioEngine::addInterface (const InterfaceConfig& cfg)
{
    for (int i = 0; i < maxInterfaces; ++i)
    {
        if (interfaces[(size_t) i]->used)
            continue;

        resetSlotDefaults (i);
        setInterfaceConfig (i, cfg);
        openInterface (i);
        return i;
    }

    return -1;
}

void AudioEngine::addDefaultInterfaceIfNone()
{
    for (auto& it : interfaces)
        if (it->used)
            return;

    // Prefer a TASCAM (ASIO first on Windows), otherwise the system default device.
    juce::StringArray typeOrder = getDeviceTypeNames();
    if (typeOrder.contains ("ASIO"))
    {
        typeOrder.removeString ("ASIO");
        typeOrder.insert (0, "ASIO");
    }

    for (auto& typeName : typeOrder)
    {
        for (auto& name : getDeviceNames (typeName, false, true))
        {
            if (name.containsIgnoreCase ("tascam") || name.containsIgnoreCase ("US-"))
            {
                InterfaceConfig cfg;
                cfg.typeName = typeName;
                cfg.outputName = name;
                cfg.inputName = getDeviceNames (typeName, true).contains (name) ? name : juce::String();
                addInterface (cfg);
                return;
            }
        }
    }

    if (auto* t = findType (getDefaultDeviceTypeName()))
    {
        t->scanForDevices();
        InterfaceConfig cfg;
        cfg.typeName = t->getTypeName();
        const auto outs = t->getDeviceNames (false);
        const auto ins = t->getDeviceNames (true);
        cfg.outputName = outs[juce::jmax (0, t->getDefaultDeviceIndex (false))];
        cfg.inputName = ins[juce::jmax (0, t->getDefaultDeviceIndex (true))];
        if (cfg.getKey().isNotEmpty())
            addInterface (cfg);
    }
}

juce::String AudioEngine::openInterface (int slot)
{
    auto& it = *interfaces[(size_t) slot];

    if (it.device != nullptr)
    {
        it.running = false;
        it.device->stop();
        it.device->close();
        it.device.reset();
    }

    if (! it.used)
        return {};

    auto* type = findType (it.config.typeName);
    if (type == nullptr)
    {
        it.status = "Driver type \"" + it.config.typeName + "\" is not available";
        return it.status;
    }

    type->scanForDevices();
    const auto inName = type->getDeviceNames (true).contains (it.config.inputName) ? it.config.inputName : juce::String();
    const auto outName = type->getDeviceNames (false).contains (it.config.outputName) ? it.config.outputName : juce::String();

    if (inName.isEmpty() && outName.isEmpty())
    {
        if (it.status != "Not connected")
        {
            it.status = "Not connected";
            bumpStructure();
        }
        return it.status;
    }

    std::unique_ptr<juce::AudioIODevice> dev (type->createDevice (outName, inName));
    if (dev == nullptr)
    {
        it.status = "Couldn't open " + it.config.getKey();
        return it.status;
    }

    auto limit = [] (int available, int cap, int wanted) { return juce::jmin (available, cap, wanted > 0 ? wanted : cap); };
    juce::BigInteger ins, outs;
    ins.setRange (0, limit (dev->getInputChannelNames().size(), maxInsPerInterface, it.config.maxIns), true);
    outs.setRange (0, limit (dev->getOutputChannelNames().size(), maxPairsPerInterface * 2, it.config.maxOuts), true);
    it.deviceIns = dev->getInputChannelNames().size();
    it.deviceOuts = dev->getOutputChannelNames().size();

    const auto rates = dev->getAvailableSampleRates();
    double rate = rates.isEmpty() ? 48000.0 : rates[0];
    for (auto wanted : { it.config.sampleRate, 48000.0 })
        if (rates.contains (wanted)) { rate = wanted; break; }

    const auto sizes = dev->getAvailableBufferSizes();
    int bufferSize = dev->getDefaultBufferSize();
    if (sizes.contains (it.config.bufferSize))
        bufferSize = it.config.bufferSize;
    else
        for (auto size : sizes)
            if (size >= it.config.bufferSize) { bufferSize = size; break; }

    auto error = dev->open (ins, outs, rate, bufferSize);
    if (error.isNotEmpty())
    {
        it.status = error;
        return error;
    }

    it.status.clear();
    it.device = std::move (dev);
    it.lastCallbackMs = 0;
    it.device->start (&it);
    it.running = true;

    openSendsForSlot (slot);
    bumpStructure();
    return {};
}

void AudioEngine::openSendsForSlot (int slot)
{
    for (int ch = 0; ch < maxInsPerInterface; ++ch)
        applySourceConfig (sourceIndex (slot, ch));

    // Every mix may use this interface's inputs, or feed its outputs.
    for (int b = 0; b < numBuses; ++b)
        applyBusConfig (b);
}

void AudioEngine::removeInterface (int slot)
{
    auto& it = *interfaces[(size_t) slot];

    if (it.device != nullptr)
    {
        it.running = false;
        it.device->stop();
        it.device->close();
        it.device.reset();
    }

    it.used = false;
    it.config = {};
    it.status.clear();
    it.numIns = 0;
    it.numOuts = 0;

    for (int ch = 0; ch < maxInsPerInterface; ++ch)
    {
        const int s = sourceIndex (slot, ch);
        sendHub.getSlot (s).enabled = false;

        for (auto& bc : busConfigs)
            bc.routes[0][s] = bc.routes[1][s] = false;
    }

    resetSlotDefaults (slot);

    for (int b = 0; b < numBuses; ++b)
        applyBusConfig (b);

    bumpStructure();
}

void AudioEngine::retryInterfaces()
{
    for (int i = 0; i < maxInterfaces; ++i)
    {
        auto& it = *interfaces[(size_t) i];
        if (! it.used)
            continue;

        const auto last = it.lastCallbackMs.load();
        const bool stale = it.running.load() && last != 0 && juce::Time::getMillisecondCounter() - last > 3000;

        if (! it.running.load() || stale)
            openInterface (i);
    }
}

juce::AudioIODeviceCallback& AudioEngine::getInterfaceCallback (int slot)
{
    return *interfaces[(size_t) slot];
}

//==============================================================================
double AudioEngine::targetMsForSource (int source) const
{
    if (source == returnSource)   return returnBufferMs;
    if (source == computerSource) return computerBufferMs;
    return crossBufferMs;
}

DriftFifo* AudioEngine::ensureCross (int source, int consumerSlot)
{
    auto& cell = cross[(size_t) source][(size_t) consumerSlot];
    if (auto* f = cell.load())
        return f;

    auto* f = crossStore.add (new DriftFifo (source >= computerSource ? (1 << 16) : (1 << 14)));
    f->setConsumerSampleRate (interfaces[(size_t) consumerSlot]->rate.load());
    f->setTargetLatencyMs (targetMsForSource (source));
    cell.store (f, std::memory_order_release);
    return f;
}

void AudioEngine::fanOut (int source, const float* l, const float* r, int n, double rate, int skipSlot) noexcept
{
    for (int c = 0; c < maxInterfaces; ++c)
        if (c != skipSlot)
            if (auto* f = cross[(size_t) source][(size_t) c].load (std::memory_order_acquire))
                f->push (l, r, n, rate);
}

//==============================================================================
bool AudioEngine::sourceExists (int s) const noexcept
{
    if (s == computerSource || s == returnSource)
        return true;

    const int slot = interfaceOfSource (s);
    return slot >= 0 && interfaces[(size_t) slot]->used && channelOfSource (s) < interfaces[(size_t) slot]->numIns.load();
}

void AudioEngine::applySourceConfig (int s)
{
    auto& c = sourceConfigs[(size_t) s];
    sourceGain[(size_t) s].store (dbToGain (c.gainDb));

    if (s == returnSource)
        return;

    bool available;
    if (s == computerSource)
        available = computerRunning.load();
    else
        available = interfaces[(size_t) interfaceOfSource (s)]->running.load() && sourceExists (s);

    auto& slot = sendHub.getSlot (s);
    sendHub.setName (s, c.name.trim().isNotEmpty() ? c.name.trim() : defaultSourceName (s));
    slot.stereo = c.sendStereo;
    slot.muted = c.sendMuted;
    slot.enabled = c.sendEnabled && available;
}

juce::String AudioEngine::getSourceDeviceLabel (int s) const
{
    if (s == computerSource) return "Computer audio";
    if (s == returnSource)   return "NDI return";
    return getInterfaceName (interfaceOfSource (s)) + juce::String::fromUTF8 (" · IN ") + juce::String (channelOfSource (s) + 1);
}

juce::String AudioEngine::getSourceKey (int s) const
{
    if (s == computerSource) return "computer";
    if (s == returnSource)   return "return";
    return interfaces[(size_t) interfaceOfSource (s)]->config.getKey() + "#" + juce::String (channelOfSource (s));
}

int AudioEngine::findSourceByKey (const juce::String& key) const
{
    if (key == "computer") return computerSource;
    if (key == "return")   return returnSource;

    const auto device = key.upToLastOccurrenceOf ("#", false, false);
    const int ch = key.fromLastOccurrenceOf ("#", false, false).getIntValue();

    for (int i = 0; i < maxInterfaces; ++i)
        if (interfaces[(size_t) i]->used && interfaces[(size_t) i]->config.getKey() == device
            && juce::isPositiveAndBelow (ch, maxInsPerInterface))
            return sourceIndex (i, ch);

    return -1;
}

bool AudioEngine::busExists (int b) const noexcept
{
    const auto& it = *interfaces[(size_t) interfaceOfBus (b)];
    return it.used && pairOfBus (b) * 2 < it.numOuts.load();
}

bool AudioEngine::busHasSecondOutput (int b) const noexcept
{
    return pairOfBus (b) * 2 + 1 < interfaces[(size_t) interfaceOfBus (b)]->numOuts.load();
}

void AudioEngine::applyBusConfig (int b)
{
    auto& bc = busConfigs[(size_t) b];
    const int slot = interfaceOfBus (b);
    const auto left = (size_t) (b * 2), right = (size_t) (b * 2 + 1);

    busStereo[(size_t) b].store (bc.stereo);
    outGain[left].store (dbToGain (bc.gainDb));
    outGain[right].store (dbToGain (bc.stereo ? bc.gainDb : bc.gainDbRight));

    for (int s = 0; s < numSources; ++s)
    {
        const bool l = bc.routes[0][s], r = bc.routes[1][s];

        // Audio from another clock needs a bridge into this interface.
        if ((l || r) && interfaceOfSource (s) != slot && interfaces[(size_t) slot]->used)
            ensureCross (s, slot);

        routeGain[(size_t) s][left].store (l ? dbToGain (bc.levelsDb[0][s]) : 0.0f);
        routeGain[(size_t) s][right].store (r ? dbToGain (bc.levelsDb[1][s]) : 0.0f);
    }
}

juce::String AudioEngine::getBusLabel (int b) const
{
    const int first = pairOfBus (b) * 2 + 1;
    return busHasSecondOutput (b) ? "Out " + juce::String (first) + "/" + juce::String (first + 1)
                                  : "Out " + juce::String (first);
}

juce::String AudioEngine::getOutputLabel (int b, int side) const
{
    return "Out " + juce::String (pairOfBus (b) * 2 + side + 1);
}

juce::String AudioEngine::getBusKey (int b) const
{
    return interfaces[(size_t) interfaceOfBus (b)]->config.getKey() + "#out" + juce::String (pairOfBus (b));
}

int AudioEngine::findBusByKey (const juce::String& key) const
{
    const auto device = key.upToLastOccurrenceOf ("#out", false, false);
    const int pair = key.fromLastOccurrenceOf ("#out", false, false).getIntValue();

    for (int i = 0; i < maxInterfaces; ++i)
        if (interfaces[(size_t) i]->used && interfaces[(size_t) i]->config.getKey() == device
            && juce::isPositiveAndBelow (pair, maxPairsPerInterface))
            return busIndex (i, pair);

    return -1;
}

//==============================================================================
static juce::String computerDeviceTypeName()
{
   #if JUCE_WINDOWS
    return "Windows Audio"; // WASAPI shared mode, where VB-CABLE lives
   #elif JUCE_MAC
    return "CoreAudio";
   #else
    return "ALSA";
   #endif
}

juce::StringArray AudioEngine::getComputerAudioDeviceNames()
{
    return getDeviceNames (computerDeviceTypeName(), true, true);
}

juce::String AudioEngine::openComputerAudioDevice (const juce::String& inputDeviceName)
{
    computerRunning = false;
    sendHub.getSlot (computerSource).enabled = false;

    if (computerDevice != nullptr)
    {
        computerDevice->stop();
        computerDevice->close();
        computerDevice.reset();
    }

    computerDeviceName = inputDeviceName;
    if (inputDeviceName.isEmpty())
        return {};

    auto* type = findType (computerDeviceTypeName());
    if (type == nullptr)
        return "No audio system available";

    type->scanForDevices();
    if (! type->getDeviceNames (true).contains (inputDeviceName))
        return "\"" + inputDeviceName + "\" is not connected";

    std::unique_ptr<juce::AudioIODevice> dev (type->createDevice ({}, inputDeviceName));
    if (dev == nullptr)
        return "Couldn't open \"" + inputDeviceName + "\"";

    const auto rates = dev->getAvailableSampleRates();
    double rate = rates.isEmpty() ? 48000.0 : rates[0];
    if (rates.contains (48000.0))
        rate = 48000.0;

    int bufferSize = dev->getDefaultBufferSize();
    for (auto size : dev->getAvailableBufferSizes())
        if (size >= 256) { bufferSize = size; break; }

    juce::BigInteger ins;
    ins.setRange (0, juce::jmin (2, dev->getInputChannelNames().size()), true);

    auto error = dev->open (ins, {}, rate, bufferSize);
    if (error.isNotEmpty())
        return error;

    for (auto& f : cross[(size_t) computerSource])
        if (auto* fifo = f.load())
            fifo->requestReset();

    computerDevice = std::move (dev);
    computerDevice->start (computerCallback.get());
    computerRunning = true;
    applySourceConfig (computerSource);
    return {};
}

juce::String AudioEngine::getComputerAudioSummary() const
{
    if (computerDevice == nullptr)
        return computerDeviceName.isEmpty() ? juce::String ("Off") : "Waiting for \"" + computerDeviceName + "\"";

    return computerDevice->getName() + juce::String::fromUTF8 (" · ") + juce::String (computerDevice->getCurrentSampleRate() / 1000.0, 1) + " kHz";
}

void AudioEngine::setReturnSource (const juce::String& fullNdiName)
{
    returnSourceName = fullNdiName;
    receiver.setSource (fullNdiName);
}

void AudioEngine::setReturnBufferMs (double ms)
{
    returnBufferMs = ms;
    for (auto& f : cross[(size_t) returnSource])
        if (auto* fifo = f.load())
            fifo->setTargetLatencyMs (ms);
}

void AudioEngine::setComputerBufferMs (double ms)
{
    computerBufferMs = ms;
    for (auto& f : cross[(size_t) computerSource])
        if (auto* fifo = f.load())
            fifo->setTargetLatencyMs (ms);
}

void AudioEngine::setCrossBufferMs (double ms)
{
    crossBufferMs = ms;
    for (int s = 0; s < computerSource; ++s)
        for (auto& f : cross[(size_t) s])
            if (auto* fifo = f.load())
                fifo->setTargetLatencyMs (ms);
}

const DriftFifo* AudioEngine::getReturnStats() const
{
    for (int c = 0; c < maxInterfaces; ++c)
        if (interfaces[(size_t) c]->running.load())
            if (auto* f = cross[(size_t) returnSource][(size_t) c].load())
                return f;

    return nullptr;
}

float AudioEngine::getAudioLoad() const noexcept
{
    float load = computerRunning.load() ? computerLoad.load() : 0.0f;
    for (auto& it : interfaces)
        if (it->running.load())
            load = juce::jmax (load, it->load.load());
    return load;
}

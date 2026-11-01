#include "AudioEngine.h"
#include "ndi/NdiLibrary.h"

using namespace bridge;

namespace
{
float peakOf (const float* data, int n) noexcept
{
    auto range = juce::FloatVectorOperations::findMinAndMax (data, n);
    return juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
}

float dbToGain (float db) noexcept
{
    return db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
}
} // namespace

//==============================================================================
AudioEngine::AudioEngine()
{
    for (int s = 0; s < numSources; ++s)
    {
        auto& c = sourceConfigs[(size_t) s];
        c.name = defaultSourceName (s);
        c.sendEnabled = (s != returnSource);
        c.sendStereo = true;
        sourceGain[(size_t) s].store (1.0f);
    }

    for (int b = 0; b < maxBuses; ++b)
    {
        auto& bc = busConfigs[(size_t) b];
        bc.name = "Headphones " + juce::String (b + 1);
        bc.routes[returnSource] = true; // everyone hears the vMix return by default
        busGain[(size_t) b].store (1.0f);

        for (auto& r : routes)
            r[(size_t) b].store (false);
    }

    outputIndexForChannel.fill (-1);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

juce::String AudioEngine::defaultSourceName (int source)
{
    if (source == computerSource) return "Computer Audio";
    if (source == returnSource)   return "vMix Return";
    return "Input " + juce::String (source + 1);
}

//==============================================================================
void AudioEngine::startup (const juce::XmlElement* savedInterfaceState)
{
    ndi::lib().load();

    sendHub.start();
    receiver.start();
    finder.start();

    interfaceManager.addAudioCallback (this);

    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.sampleRate = 48000.0; // NDI and vMix run at 48 kHz
    preferred.bufferSize = 128;

    if (savedInterfaceState != nullptr)
    {
        interfaceManager.initialise (maxInputs, maxBuses * 2, savedInterfaceState, true, {}, &preferred);
    }
    else
    {
        interfaceManager.initialise (maxInputs, maxBuses * 2, nullptr, true);
        selectTascamIfPresent();
    }

    interfaceManager.addChangeListener (this);

    for (int s = 0; s < numSources; ++s)
        applySourceConfig (s);

    for (int b = 0; b < maxBuses; ++b)
        applyBusConfig (b);

    receiver.setSource (returnSourceName);
    returnFifo.setTargetLatencyMs (returnBufferMs);
    computerFifo.setTargetLatencyMs (computerBufferMs);
}

void AudioEngine::selectTascamIfPresent()
{
    // First run: pick a TASCAM interface if one is connected (ASIO first on Windows).
    juce::Array<juce::AudioIODeviceType*> types;
    for (auto* type : interfaceManager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() == "ASIO")
            types.insert (0, type);
        else
            types.add (type);
    }

    for (auto* type : types)
    {
        type->scanForDevices();

        for (auto& name : type->getDeviceNames (false))
        {
            if (! (name.containsIgnoreCase ("tascam") || name.containsIgnoreCase ("US-")))
                continue;

            interfaceManager.setCurrentAudioDeviceType (type->getTypeName(), true);

            auto setup = interfaceManager.getAudioDeviceSetup();
            setup.outputDeviceName = name;
            if (type->getDeviceNames (true).contains (name))
                setup.inputDeviceName = name;
            setup.sampleRate = 48000.0;
            setup.bufferSize = 128;
            setup.useDefaultInputChannels = true;
            setup.useDefaultOutputChannels = true;
            interfaceManager.setAudioDeviceSetup (setup, true);
            return;
        }
    }
}

void AudioEngine::shutdown()
{
    interfaceManager.removeChangeListener (this);
    interfaceManager.removeAudioCallback (this);
    interfaceManager.closeAudioDevice();

    openComputerAudioDevice ({});
    computerDeviceName.clear();

    finder.stop();
    receiver.stop();
    sendHub.stop();

    ndi::lib().shutdown();
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The interface changed (different device / channel count): re-evaluate
    // which NDI sends should exist.
    for (int s = 0; s < numSources; ++s)
        applySourceConfig (s);
}

//==============================================================================
juce::String AudioEngine::getInputChannelName (int index) const
{
    if (auto* d = interfaceManager.getCurrentAudioDevice())
    {
        auto names = d->getInputChannelNames();
        if (juce::isPositiveAndBelow (index, names.size()))
            return names[index];
    }

    return "In " + juce::String (index + 1);
}

juce::String AudioEngine::getBusChannelLabel (int bus) const
{
    const int first = bus * 2 + 1;
    int numOuts = 0;

    if (auto* d = interfaceManager.getCurrentAudioDevice())
        numOuts = d->getOutputChannelNames().size();

    if (first + 1 <= numOuts)
        return "Out " + juce::String (first) + "/" + juce::String (first + 1);

    return "Out " + juce::String (first);
}

juce::String AudioEngine::getInterfaceSummary() const
{
    if (auto* d = interfaceManager.getCurrentAudioDevice())
    {
        const auto sr = d->getCurrentSampleRate();
        const auto bs = d->getCurrentBufferSizeSamples();
        return d->getName() + "  |  " + juce::String (sr / 1000.0, 1) + " kHz  |  "
               + juce::String (bs) + " samples (" + juce::String (bs / sr * 1000.0, 1) + " ms)"
               + "  |  " + juce::String (d->getInputChannelNames().size()) + " in / "
               + juce::String (d->getOutputChannelNames().size()) + " out";
    }

    return "No audio interface open - click Audio Interface to choose one";
}

//==============================================================================
juce::AudioIODeviceType* AudioEngine::getComputerDeviceType()
{
    if (computerTypes.isEmpty())
        interfaceManager.createAudioDeviceTypes (computerTypes);

   #if JUCE_WINDOWS
    const juce::String preferred = "Windows Audio"; // WASAPI shared mode - where VB-CABLE lives
   #elif JUCE_MAC
    const juce::String preferred = "CoreAudio";
   #else
    const juce::String preferred = "ALSA";
   #endif

    for (auto* t : computerTypes)
        if (t->getTypeName() == preferred)
            return t;

    return computerTypes.getFirst();
}

juce::StringArray AudioEngine::getComputerAudioDeviceNames()
{
    juce::StringArray names;

    if (auto* type = getComputerDeviceType())
    {
        type->scanForDevices();
        names = type->getDeviceNames (true);
    }

    return names;
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

    auto* type = getComputerDeviceType();
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
    for (auto wanted : { 48000.0, sampleRate })
        if (rates.contains (wanted)) { rate = wanted; break; }

    int bufferSize = dev->getDefaultBufferSize();
    for (auto size : dev->getAvailableBufferSizes())
        if (size >= 256) { bufferSize = size; break; }

    juce::BigInteger ins;
    ins.setRange (0, juce::jmin (2, dev->getInputChannelNames().size()), true);

    auto error = dev->open (ins, {}, rate, bufferSize);
    if (error.isNotEmpty())
        return error;

    computerFifo.requestReset();
    computerDevice = std::move (dev);
    computerDevice->start (&computerCallback);
    computerRunning = true;
    applySourceConfig (computerSource);
    return {};
}

juce::String AudioEngine::getComputerAudioSummary() const
{
    if (computerDevice == nullptr)
        return computerDeviceName.isEmpty() ? juce::String ("Off") : "Waiting for \"" + computerDeviceName + "\"";

    return computerDevice->getName() + "  |  " + juce::String (computerDevice->getCurrentSampleRate() / 1000.0, 1) + " kHz";
}

//==============================================================================
void AudioEngine::applySourceConfig (int s)
{
    auto& c = sourceConfigs[(size_t) s];
    sourceGain[(size_t) s].store (dbToGain (c.gainDb));

    if (s == returnSource)
        return;

    const bool available = (s == computerSource) ? computerRunning.load() : (s < numInputs.load());

    auto& slot = sendHub.getSlot (s);
    sendHub.setName (s, c.name.trim().isNotEmpty() ? c.name.trim() : defaultSourceName (s));
    slot.stereo = c.sendStereo;
    slot.muted = c.sendMuted;
    slot.enabled = c.sendEnabled && available;
}

void AudioEngine::applyBusConfig (int b)
{
    auto& bc = busConfigs[(size_t) b];
    busGain[(size_t) b].store (dbToGain (bc.gainDb));

    for (int s = 0; s < numSources; ++s)
        routes[(size_t) s][(size_t) b].store (bc.routes[s]);
}

void AudioEngine::setReturnSource (const juce::String& fullNdiName)
{
    returnSourceName = fullNdiName;
    receiver.setSource (fullNdiName);
}

void AudioEngine::setReturnBufferMs (double ms)
{
    returnBufferMs = ms;
    returnFifo.setTargetLatencyMs (ms);
}

void AudioEngine::setComputerBufferMs (double ms)
{
    computerBufferMs = ms;
    computerFifo.setTargetLatencyMs (ms);
}

//==============================================================================
// Interface (TASCAM) callback
//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();

    const auto ins = device->getActiveInputChannels();
    const auto outs = device->getActiveOutputChannels();

    numActiveInputs = 0;
    outputIndexForChannel.fill (-1);
    int outIndex = 0;

    for (int ch = 0; ch < maxDeviceChannels; ++ch)
    {
        if (ins[ch])
            activeInputChannel[(size_t) numActiveInputs++] = ch;

        if (outs[ch])
            outputIndexForChannel[(size_t) ch] = outIndex++;
    }

    // Rows/columns follow the channels enabled in the Audio Interface dialog.
    numInputs = juce::jlimit (0, maxInputs, ins.getHighestBit() + 1);
    numBuses = juce::jlimit (0, maxBuses, (outs.getHighestBit() + 2) / 2);

    inputScratch.setSize (maxInputs, chunkSize);
    mixScratch.setSize (6, chunkSize);

    returnFifo.setConsumerSampleRate (sampleRate);
    computerFifo.setConsumerSampleRate (sampleRate);
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                                    float* const* out, int numOut,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int i = 0; i < numOut; ++i)
        if (out[i] != nullptr)
            juce::FloatVectorOperations::clear (out[i], numSamples);

    for (int offset = 0; offset < numSamples; offset += chunkSize)
        processChunk (in, numIn, out, numOut, offset, juce::jmin (chunkSize, numSamples - offset));
}

void AudioEngine::processChunk (const float* const* in, int numIn, float* const* out, int numOut,
                                int offset, int n) noexcept
{
    // 1. Interface inputs -> gain -> NDI sends (and kept for the headphone mix)
    inputPresent.fill (false);

    for (int a = 0; a < juce::jmin (numIn, numActiveInputs); ++a)
    {
        const int ch = activeInputChannel[(size_t) a];
        if (ch >= maxInputs || in[a] == nullptr)
            continue;

        float* buf = inputScratch.getWritePointer (ch);
        juce::FloatVectorOperations::copyWithMultiply (buf, in[a] + offset, sourceGain[(size_t) ch].load(), n);
        sourceMeters[(size_t) ch].update (peakOf (buf, n));
        sendHub.getSlot (ch).push (buf, nullptr, n, sampleRate);
        inputPresent[(size_t) ch] = true;
    }

    // 2. NDI return from vMix (drift-corrected to this device's clock)
    float* retL = mixScratch.getWritePointer (0);
    float* retR = mixScratch.getWritePointer (1);
    returnFifo.pull (retL, retR, n);
    const float retGain = sourceGain[(size_t) returnSource].load();
    juce::FloatVectorOperations::multiply (retL, retGain, n);
    juce::FloatVectorOperations::multiply (retR, retGain, n);
    sourceMeters[(size_t) returnSource].update (juce::jmax (peakOf (retL, n), peakOf (retR, n)));

    // 3. Computer audio (gain already applied on its own device thread)
    float* compL = mixScratch.getWritePointer (2);
    float* compR = mixScratch.getWritePointer (3);
    const bool haveComputer = computerRunning.load();
    if (haveComputer)
        computerFifo.pull (compL, compR, n);

    // 4. Headphone matrix -> output pairs
    float* busL = mixScratch.getWritePointer (4);
    float* busR = mixScratch.getWritePointer (5);

    for (int b = 0; b < numBuses.load(); ++b)
    {
        const int chL = b * 2, chR = b * 2 + 1;
        const int idxL = outputIndexForChannel[(size_t) chL];
        const int idxR = chR < maxDeviceChannels ? outputIndexForChannel[(size_t) chR] : -1;

        float* outL = (idxL >= 0 && idxL < numOut) ? out[idxL] : nullptr;
        float* outR = (idxR >= 0 && idxR < numOut) ? out[idxR] : nullptr;

        if (outL == nullptr && outR == nullptr)
            continue;

        juce::FloatVectorOperations::clear (busL, n);
        juce::FloatVectorOperations::clear (busR, n);

        for (int s = 0; s < maxInputs; ++s)
        {
            if (inputPresent[(size_t) s] && routes[(size_t) s][(size_t) b].load (std::memory_order_relaxed))
            {
                const float* src = inputScratch.getReadPointer (s);
                juce::FloatVectorOperations::add (busL, src, n);
                juce::FloatVectorOperations::add (busR, src, n);
            }
        }

        if (haveComputer && routes[(size_t) computerSource][(size_t) b].load (std::memory_order_relaxed))
        {
            juce::FloatVectorOperations::add (busL, compL, n);
            juce::FloatVectorOperations::add (busR, compR, n);
        }

        if (routes[(size_t) returnSource][(size_t) b].load (std::memory_order_relaxed))
        {
            juce::FloatVectorOperations::add (busL, retL, n);
            juce::FloatVectorOperations::add (busR, retR, n);
        }

        const float g = busGain[(size_t) b].load();
        juce::FloatVectorOperations::multiply (busL, g, n);
        juce::FloatVectorOperations::multiply (busR, g, n);

        busMeters[(size_t) (b * 2)].update (peakOf (busL, n));
        busMeters[(size_t) (b * 2 + 1)].update (peakOf (busR, n));

        if (outL != nullptr && outR != nullptr)
        {
            juce::FloatVectorOperations::copy (outL + offset, busL, n);
            juce::FloatVectorOperations::copy (outR + offset, busR, n);
        }
        else
        {
            // Odd output count: last bus is mono.
            float* mono = outL != nullptr ? outL : outR;
            juce::FloatVectorOperations::copy (mono + offset, busL, n);
            juce::FloatVectorOperations::add (mono + offset, busR, n);
            juce::FloatVectorOperations::multiply (mono + offset, 0.5f, n);
        }
    }
}

//==============================================================================
// Computer audio (virtual device) callback
//==============================================================================
void AudioEngine::ComputerCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    scratchSize = 2048;
    scratchL.allocate ((size_t) scratchSize, true);
    scratchR.allocate ((size_t) scratchSize, true);
}

void AudioEngine::ComputerCallback::audioDeviceStopped() {}

void AudioEngine::ComputerCallback::audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                                                     float* const* out, int numOut,
                                                                     int numSamples,
                                                                     const juce::AudioIODeviceCallbackContext&)
{
    for (int i = 0; i < numOut; ++i)
        if (out[i] != nullptr)
            juce::FloatVectorOperations::clear (out[i], numSamples);

    if (numIn < 1 || in[0] == nullptr || scratchSize == 0)
        return;

    const float* l = in[0];
    const float* r = (numIn > 1 && in[1] != nullptr) ? in[1] : in[0];
    const float gain = engine.sourceGain[(size_t) computerSource].load();

    for (int offset = 0; offset < numSamples; offset += scratchSize)
    {
        const int n = juce::jmin (scratchSize, numSamples - offset);
        juce::FloatVectorOperations::copyWithMultiply (scratchL.get(), l + offset, gain, n);
        juce::FloatVectorOperations::copyWithMultiply (scratchR.get(), r + offset, gain, n);

        engine.sourceMeters[(size_t) computerSource].update (juce::jmax (peakOf (scratchL.get(), n),
                                                                         peakOf (scratchR.get(), n)));

        engine.sendHub.getSlot (computerSource).push (scratchL.get(), scratchR.get(), n, sampleRate);
        engine.computerFifo.push (scratchL.get(), scratchR.get(), n, sampleRate);
    }
}

// Headless checks that run without audio hardware:
//   1. DriftFifo: clock drift, bursty network delivery and 48k->44.1k conversion
//      must produce a clean, continuous tone with no dropouts.
//   2. Headphone matrix: per-side routing in STEREO and 2x MONO pairs, driving
//      the real AudioEngine callback with a fake 4-in / 4-out interface.
//   3. NDI loopback (only if an NDI runtime is installed): send a tone through
//      NdiSendHub, receive it with NdiReceiver, and check it arrives intact.

#include "dsp/DriftFifo.h"
#include "engine/AudioEngine.h"
#include "ndi/NdiFinder.h"
#include "ndi/NdiLibrary.h"
#include "ndi/NdiReceiver.h"
#include "ndi/NdiSendHub.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

/** Fraction of signal energy at `freq` (1.0 = pure tone). */
double tonePurity (const std::vector<float>& x, double freq, double rate)
{
    double re = 0, im = 0, energy = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double ph = 2.0 * juce::MathConstants<double>::pi * freq * (double) i / rate;
        re += x[i] * std::cos (ph);
        im += x[i] * std::sin (ph);
        energy += (double) x[i] * x[i];
    }
    const double toneEnergy = 2.0 * (re * re + im * im) / (double) x.size();
    return energy > 0 ? toneEnergy / energy : 0.0;
}

/** Purity at the best-matching frequency near f0 (the tone legitimately shifts
    by the clock drift, so a fixed-frequency check would be unfair). */
double bestTonePurity (const std::vector<float>& x, double f0, double rate)
{
    double best = 0;
    for (double f = f0 - 2.0; f <= f0 + 2.0; f += 0.02)
        best = std::max (best, tonePurity (x, f, rate));
    return best;
}

/** Simulates a producer on another clock delivering bursts into a consumer callback. */
void driftScenario (const char* name, double producerRate, double drift, double consumerRate,
                    int burst, int block, double targetMs, double jitterMs)
{
    std::printf ("DriftFifo: %s\n", name);

    DriftFifo fifo;
    fifo.setConsumerSampleRate (consumerRate);
    fifo.setTargetLatencyMs (targetMs);

    const double toneHz = 997.0;
    const double actualProducerRate = producerRate * (1.0 + drift); // the real (drifting) clock
    std::mt19937 rng (42);
    std::uniform_real_distribution<double> jitter (0.0, jitterMs / 1000.0);

    std::vector<float> l ((size_t) burst), r ((size_t) burst), outL ((size_t) block), outR ((size_t) block);
    std::vector<float> tail;
    float prev1 = 0, prev2 = 0, maxCurvature = 0;
    const double simSeconds = 600.0; // 10 minutes
    double t = 0, nextBurstTime = 0;
    long long produced = 0;
    int underrunsAfterStart = -1;

    while (t < simSeconds)
    {
        // Deliver every burst whose (jittered) arrival time has passed.
        while (nextBurstTime <= t)
        {
            for (int i = 0; i < burst; ++i)
            {
                const double ph = 2.0 * juce::MathConstants<double>::pi * toneHz * (double) (produced + i) / producerRate;
                l[(size_t) i] = 0.5f * (float) std::sin (ph);
                r[(size_t) i] = 0.25f * (float) std::sin (ph);
            }
            produced += burst;
            fifo.push (l.data(), r.data(), burst, producerRate);
            nextBurstTime = (double) produced / actualProducerRate + jitter (rng);
        }

        fifo.pull (outL.data(), outR.data(), block);
        t += block / consumerRate;

        if (underrunsAfterStart < 0 && t > 5.0)
            underrunsAfterStart = fifo.getUnderruns();

        if (t > 5.0)
        {
            // A click or discontinuity shows up as a spike in the second difference.
            for (auto v : outL)
            {
                maxCurvature = std::max (maxCurvature, std::abs (v - 2.0f * prev1 + prev2));
                prev2 = prev1;
                prev1 = v;
            }
        }
        else if (! outL.empty())
        {
            prev2 = outL[outL.size() - 2];
            prev1 = outL.back();
        }

        if (t > simSeconds - 0.5)
            tail.insert (tail.end(), outL.begin(), outL.end());
    }

    const int dropouts = fifo.getUnderruns() - underrunsAfterStart;
    const double purity = bestTonePurity (tail, toneHz, consumerRate);
    const double w = 2.0 * juce::MathConstants<double>::pi * toneHz / consumerRate;
    const double curvatureLimit = 0.5 * w * w * 1.5; // 0.5 amplitude sine, 50% headroom
    const double buffered = fifo.getBufferedMs();

    check (dropouts == 0, "no dropouts over 10 simulated minutes (got " + juce::String (dropouts) + ")");
    check (fifo.getOverruns() == 0, "no overruns (got " + juce::String (fifo.getOverruns()) + ")");
    check (std::abs (buffered - targetMs) < targetMs * 0.5 + 10.0,
           "latency held near target: " + juce::String (buffered, 1) + " ms vs " + juce::String (targetMs) + " ms");
    check (purity > 0.9999, "tone is clean, no wobble (purity " + juce::String (purity, 6) + ")");
    check (maxCurvature < curvatureLimit, "no clicks after start-up (max 2nd diff " + juce::String (maxCurvature, 5)
                                            + ", limit " + juce::String (curvatureLimit, 5) + ")");
    std::printf ("    correction now %.0f ppm (expected about %.0f)\n", fifo.getCorrectionPpm(),
                 (actualProducerRate / producerRate - 1.0) * 1e6);
}


/** A stand-in interface: 4 inputs, 4 outputs, 48 kHz, all channels active. */
struct FakeInterface : juce::AudioIODevice
{
    FakeInterface() : juce::AudioIODevice ("Fake TASCAM", "Test") {}
    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2", "Out 3", "Out 4" }; }
    juce::StringArray getInputChannelNames() override  { return { "In 1", "In 2", "In 3", "In 4" }; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override  { return { 256 }; }
    int getDefaultBufferSize() override { return 256; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 256; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { juce::BigInteger b; b.setRange (0, 4, true); return b; }
    juce::BigInteger getActiveInputChannels() const override  { juce::BigInteger b; b.setRange (0, 4, true); return b; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};

void routingMatrix()
{
    std::printf ("Headphone matrix routing\n");

    juce::ScopedJuceInitialiser_GUI juceInit; // AudioDeviceManager needs the message system
    AudioEngine engine;
    FakeInterface device;
    auto& cb = engine.getInterfaceCallback();
    cb.audioDeviceAboutToStart (&device);

    // Constant test signals: In 1 = 0.1, In 2 = 0.2; vMix return L = 0.5, R = 0.25.
    constexpr int block = 256;
    std::vector<float> in1 (block, 0.1f), in2 (block, 0.2f), in3 (block, 0.0f), in4 (block, 0.0f);
    std::vector<std::vector<float>> outs (4, std::vector<float> (block));
    const float* ins[] = { in1.data(), in2.data(), in3.data(), in4.data() };
    float* outPtrs[] = { outs[0].data(), outs[1].data(), outs[2].data(), outs[3].data() };

    // The vMix return arrives continuously, one block per callback.
    engine.setReturnBufferMs (10.0);
    std::vector<float> retL (block, 0.5f), retR (block, 0.25f);

    auto run = [&]
    {
        for (int i = 0; i < 8; ++i) // let the return feed settle
        {
            engine.getReturnFifo().push (retL.data(), retR.data(), block, 48000.0);
            cb.audioDeviceIOCallbackWithContext (ins, 4, outPtrs, 4, block, {});
        }
    };
    auto near = [] (float a, float b) { return std::abs (a - b) < 1.0e-3f; };
    auto out = [&] (int ch) { return outs[(size_t) ch][block - 1]; };

    // Pair 1 (Out 1/2) STEREO: In 1 on the left only, return on both sides.
    auto& p1 = engine.getBusConfig (0);
    p1 = {};
    p1.stereo = true;
    p1.routes[0][0] = true;
    p1.routes[0][bridge::returnSource] = p1.routes[1][bridge::returnSource] = true;
    engine.applyBusConfig (0);

    // Pair 2 (Out 3/4) 2x MONO: Out 3 = In 2 + return, Out 4 = In 1 at -6 dB.
    auto& p2 = engine.getBusConfig (1);
    p2 = {};
    p2.stereo = false;
    p2.routes[0][1] = true;
    p2.routes[0][bridge::returnSource] = true;
    p2.routes[1][0] = true;
    p2.gainDbRight = -6.0206f;
    engine.applyBusConfig (1);

    run();
    check (near (out (0), 0.6f),   "stereo pair: Out 1 = In 1 + return L (" + juce::String (out (0), 4) + ")");
    check (near (out (1), 0.25f),  "stereo pair: Out 2 = return R only, In 1 not on the right (" + juce::String (out (1), 4) + ")");
    check (near (out (2), 0.575f), "2x mono: Out 3 = In 2 + return folded to mono (" + juce::String (out (2), 4) + ")");
    check (near (out (3), 0.05f),  "2x mono: Out 4 = In 1 with its own -6 dB level (" + juce::String (out (3), 4) + ")");

    // Per-source levels: In 1 at -6 dB on Out 1 only; return at -6 dB into Out 4's mix.
    p1.levelsDb[0][0] = -6.0206f;
    engine.applyBusConfig (0);
    p2.routes[1][bridge::returnSource] = true;
    p2.levelsDb[1][bridge::returnSource] = -6.0206f;
    engine.applyBusConfig (1);
    run();
    check (near (out (0), 0.55f),   "per-source level: In 1 at -6 dB into Out 1 (" + juce::String (out (0), 4) + ")");
    check (near (out (3), 0.14375f), "per-source level: return at -6 dB into Out 4, then Out 4 at -6 dB (" + juce::String (out (3), 4) + ")");
    p1.levelsDb[0][0] = 0.0f;
    p2.routes[1][bridge::returnSource] = false;
    engine.applyBusConfig (0);
    engine.applyBusConfig (1);

    // Same routes, pair 1 switched to 2x MONO: the return folds to mono on Out 2.
    p1.stereo = false;
    engine.applyBusConfig (0);
    run();
    check (near (out (1), 0.375f), "pair switched to 2x mono: Out 2 = mono return (" + juce::String (out (1), 4) + ")");
    check (near (out (0), 0.475f), "pair switched to 2x mono: Out 1 = In 1 + mono return (" + juce::String (out (0), 4) + ")");

    cb.audioDeviceStopped();
}

void ndiLoopback()
{
    std::printf ("NDI loopback\n");

    auto& lib = ndi::lib();
    if (! lib.load())
    {
        std::printf ("  [SKIP] %s\n", lib.getStatusText().toRawUTF8());
        return;
    }

    std::printf ("  runtime: %s (%s)\n", lib.getStatusText().toRawUTF8(), lib.getLoadedPath().toRawUTF8());

    {
        NdiSendHub hub;
        NdiFinder finder;
        DriftFifo fifo;
        fifo.setConsumerSampleRate (48000.0);
        fifo.setTargetLatencyMs (60.0);
        NdiReceiver receiver (fifo);

        const auto uniqueName = "NDIAB SelfTest " + juce::String (juce::Random::getSystemRandom().nextInt (99999));
        hub.setName (0, uniqueName);
        hub.getSlot (0).stereo = true;
        hub.getSlot (0).enabled = true;
        hub.start();
        finder.start();
        receiver.start();

        // A fake "interface" thread: push a 440 Hz tone in 5 ms blocks, in real time.
        std::atomic<bool> running { true };
        std::thread producer ([&]
        {
            std::vector<float> buf (240);
            long long n = 0;
            auto start = juce::Time::getMillisecondCounterHiRes();
            while (running)
            {
                for (auto& s : buf)
                    s = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) n++ / 48000.0);
                hub.getSlot (0).push (buf.data(), nullptr, (int) buf.size(), 48000.0);
                const double due = start + (double) n / 48.0;
                const double wait = due - juce::Time::getMillisecondCounterHiRes();
                if (wait > 0) juce::Thread::sleep ((int) wait);
            }
        });

        // Find the source.
        juce::String found;
        for (int i = 0; i < 100 && found.isEmpty(); ++i)
        {
            for (auto& s : finder.getSources())
                if (s.contains ("(" + uniqueName + ")"))
                    found = s;
            juce::Thread::sleep (100);
        }

        check (found.isNotEmpty(), "sender discovered via NDI finder" + (found.isNotEmpty() ? " as \"" + found + "\"" : juce::String()));

        if (found.isNotEmpty())
        {
            receiver.setSource (found);

            // Fake "interface" consumer pulling 128-sample blocks in real time.
            std::vector<float> outL (128), outR (128), captured;
            auto start = juce::Time::getMillisecondCounterHiRes();
            long long pulled = 0;
            while (juce::Time::getMillisecondCounterHiRes() - start < 6000.0)
            {
                fifo.pull (outL.data(), outR.data(), 128);
                pulled += 128;
                if (juce::Time::getMillisecondCounterHiRes() - start > 4000.0)
                    captured.insert (captured.end(), outL.begin(), outL.end());
                const double due = start + (double) pulled / 48.0;
                const double wait = due - juce::Time::getMillisecondCounterHiRes();
                if (wait > 0) juce::Thread::sleep ((int) wait);
            }

            check (receiver.isReceiving(), "receiver is getting audio");
            check (receiver.getSourceChannels() == 2, "stereo source arrives as 2 channels");
            check (hub.getSlot (0).connections.load() >= 1, "sender sees the receiver connection");

            float peak = 0;
            for (auto v : captured) peak = juce::jmax (peak, std::abs (v));
            const double purity = bestTonePurity (captured, 440.0, 48000.0);
            check (peak > 0.45f && peak < 0.55f, "level preserved end-to-end (peak " + juce::String (peak, 3) + ")");
            check (purity > 0.995, "440 Hz tone arrives clean (purity " + juce::String (purity, 5) + ")");
            check (fifo.getUnderruns() <= 1, "no dropouts once running (underruns " + juce::String (fifo.getUnderruns()) + ")");
            std::printf ("    buffered %.1f ms\n", fifo.getBufferedMs());
        }

        running = false;
        producer.join();
        receiver.stop();
        finder.stop();
        hub.stop();
    }

    lib.shutdown();
}
} // namespace

int main()
{
    driftScenario ("vMix return, 30 fps bursts, +150 ppm", 48000.0, 150e-6, 48000.0, 1600, 128, 60.0, 8.0);
    driftScenario ("vMix return, 25 fps bursts, -300 ppm", 48000.0, -300e-6, 48000.0, 1920, 256, 60.0, 10.0);
    driftScenario ("computer audio, 512 blocks, +80 ppm", 48000.0, 80e-6, 48000.0, 512, 128, 30.0, 2.0);
    driftScenario ("48 kHz NDI into 44.1 kHz interface", 48000.0, 50e-6, 44100.0, 1600, 256, 60.0, 8.0);

    routingMatrix();
    ndiLoopback();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

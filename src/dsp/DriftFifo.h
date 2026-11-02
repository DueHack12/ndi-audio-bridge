#pragma once

#include "StereoFifo.h"

#include <atomic>
#include <cmath>

/** Bridges stereo audio between two independent clocks.

    A producer (an NDI receive thread, or a second audio device such as the
    virtual "computer audio" device) pushes audio at its own pace and sample
    rate. The consumer (the interface's audio callback) pulls exactly the number
    of samples it needs.

    The consumer resamples with a 4-point Hermite interpolator and nudges the
    playback ratio (by a few hundred ppm, at most 0.5%) so the fill level hovers around the
    target latency. That absorbs clock drift between, say, the Tascam's crystal
    and the vMix machine without clicks or slowly growing delay. It also handles
    a sample-rate mismatch (e.g. 48 kHz NDI into a 44.1 kHz device).
*/
class DriftFifo
{
public:
    /** Capacity is allocated once and never resized (1 << 16 = ~1.3 s at 48 kHz). */
    explicit DriftFifo (int capacityFrames = 1 << 16)
    {
        fifo.prepare (capacityFrames);
    }

    /** Call while the consumer is stopped (e.g. from audioDeviceAboutToStart). */
    void setConsumerSampleRate (double rate)
    {
        consumerRate = rate > 0 ? rate : 48000.0;
        requestReset();
    }

    /** Any thread. Clamped to 5..500 ms. */
    void setTargetLatencyMs (double ms)
    {
        targetMs.store (std::clamp (ms, 5.0, 500.0));
    }

    double getTargetLatencyMs() const noexcept { return targetMs.load(); }

    // ------------------------------------------------------------------ producer

    /** Producer: push planar audio. `r` may be null for mono. */
    void push (const float* l, const float* r, int numSamples, double producerSampleRate) noexcept
    {
        if (producerSampleRate > 0)
            producerRate.store (producerSampleRate, std::memory_order_relaxed);

        // If the buffer is full, the consumer has stalled; drop rather than block.
        if (! fifo.push (l, r, numSamples))
            overflows.fetch_add (1, std::memory_order_relaxed);
    }

    /** Producer or message thread: ask the consumer to flush and re-prime. */
    void requestReset() noexcept { resetRequested.store (true); }

    // ------------------------------------------------------------------ consumer

    /** Consumer (real-time audio thread): fill numSamples of L/R. */
    void pull (float* outL, float* outR, int numSamples) noexcept
    {
        if (resetRequested.exchange (false))
        {
            fifo.discard();
            resetConsumerState();
        }

        const double srcRate = producerRate.load (std::memory_order_relaxed);
        const double nominal = srcRate / consumerRate;
        const double target = targetMs.load() * 0.001 * srcRate;

        // Fill level, counting the frames already pulled into the interpolator history.
        const double fill = (double) fifo.available() + (2.0 - frac);

        if (! running)
        {
            if (fill >= target)
            {
                running = true;
                smoothedFill = smoothedFill2 = fill;
            }
            else
            {
                std::fill (outL, outL + numSamples, 0.0f);
                std::fill (outR, outR + numSamples, 0.0f);
                return;
            }
        }

        // Far too much buffered (e.g. after the producer burst or we stalled):
        // jump forward instead of slowly catching up.
        if (fill > target * 2.0 + 0.1 * srcRate)
        {
            fifo.discard ((int) (fill - target));
            overruns.fetch_add (1, std::memory_order_relaxed);
            smoothedFill = smoothedFill2 = target;
        }

        // Smooth the fill level with two cascaded one-pole filters (1 s each).
        // Network audio arrives in bursts (e.g. one 33 ms chunk per vMix frame),
        // so the raw fill level is a sawtooth; the second pole keeps that ripple
        // from turning into audible pitch wobble.
        const double blockSeconds = numSamples / consumerRate;
        const double alpha = std::min (1.0, blockSeconds / 1.0);
        smoothedFill += alpha * (fill - smoothedFill);
        smoothedFill2 += alpha * (smoothedFill - smoothedFill2);

        const double errorSeconds = (smoothedFill2 - target) / srcRate;
        // Remove the error over ~10 s: slow enough that network jitter does not
        // wobble the pitch, fast enough to track any real clock drift.
        const double correction = std::clamp (errorSeconds / 10.0, -0.005, 0.005);
        ratio = nominal * (1.0 + correction);
        currentCorrectionPpm.store ((float) (correction * 1.0e6), std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            frac += ratio;

            while (frac >= 1.0)
            {
                if (! advance())
                {
                    // Ran dry: output silence for the rest of the block and re-prime.
                    std::fill (outL + i, outL + numSamples, 0.0f);
                    std::fill (outR + i, outR + numSamples, 0.0f);
                    underruns.fetch_add (1, std::memory_order_relaxed);
                    resetConsumerState();
                    return;
                }

                frac -= 1.0;
            }

            outL[i] = hermite (hl, (float) frac);
            outR[i] = hermite (hr, (float) frac);
        }

        bufferedMs.store ((float) (smoothedFill2 / srcRate * 1000.0), std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------ stats (any thread)

    float getBufferedMs() const noexcept      { return running ? bufferedMs.load() : 0.0f; }
    float getCorrectionPpm() const noexcept   { return currentCorrectionPpm.load(); }
    int getUnderruns() const noexcept         { return underruns.load(); }
    int getOverruns() const noexcept          { return overruns.load() + overflows.load(); }
    bool isRunning() const noexcept           { return running; }

private:
    bool advance() noexcept
    {
        if (fifo.available() < 1)
            return false;

        float l, r;
        fifo.read (&l, &r, 1);

        hl[0] = hl[1]; hl[1] = hl[2]; hl[2] = hl[3]; hl[3] = l;
        hr[0] = hr[1]; hr[1] = hr[2]; hr[2] = hr[3]; hr[3] = r;
        return true;
    }

    // Interpolates between y[1] and y[2].
    static float hermite (const float* y, float t) noexcept
    {
        const float c0 = y[1];
        const float c1 = 0.5f * (y[2] - y[0]);
        const float c2 = y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3];
        const float c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    void resetConsumerState() noexcept
    {
        running = false;
        frac = 0.0;
        ratio = 1.0;
        smoothedFill = smoothedFill2 = 0.0;
        std::fill (std::begin (hl), std::end (hl), 0.0f);
        std::fill (std::begin (hr), std::end (hr), 0.0f);
        bufferedMs.store (0.0f);
        currentCorrectionPpm.store (0.0f);
    }

    StereoFifo fifo;
    double consumerRate = 48000.0;
    std::atomic<double> producerRate { 48000.0 };
    std::atomic<double> targetMs { 40.0 };
    std::atomic<bool> resetRequested { false };

    // consumer-only state (running is also read by the UI)
    std::atomic<bool> running { false };
    double frac = 0.0, ratio = 1.0, smoothedFill = 0.0, smoothedFill2 = 0.0;
    float hl[4] {}, hr[4] {};

    std::atomic<float> bufferedMs { 0.0f }, currentCorrectionPpm { 0.0f };
    std::atomic<int> underruns { 0 }, overruns { 0 }, overflows { 0 };
};

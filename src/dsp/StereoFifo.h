#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>

/** Single-producer / single-consumer lock-free stereo sample FIFO.

    The producer (an audio callback) writes, the consumer (a worker thread)
    reads. Neither side ever blocks or allocates after prepare().
*/
class StereoFifo
{
public:
    void prepare (int capacityFrames)
    {
        capacity = (int) nextPow2 ((uint32_t) std::max (capacityFrames, 64));
        mask = capacity - 1;
        left.assign ((size_t) capacity, 0.0f);
        right.assign ((size_t) capacity, 0.0f);
        writePos.store (0);
        readPos.store (0);
    }

    int getCapacity() const noexcept { return capacity; }

    /** Producer side. Returns false if the data did not fit (it is dropped). */
    bool push (const float* l, const float* r, int n, float gain = 1.0f) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        const auto rd = readPos.load (std::memory_order_acquire);

        if ((int64_t) (w - rd) + n > capacity)
            return false;

        for (int i = 0; i < n; ++i)
        {
            const auto idx = (size_t) ((w + (uint64_t) i) & (uint64_t) mask);
            left[idx] = l != nullptr ? l[i] * gain : 0.0f;
            right[idx] = r != nullptr ? r[i] * gain : left[idx];
        }

        writePos.store (w + (uint64_t) n, std::memory_order_release);
        return true;
    }

    /** Consumer side. */
    int available() const noexcept
    {
        return (int) (writePos.load (std::memory_order_acquire) - readPos.load (std::memory_order_relaxed));
    }

    /** Consumer side: reads exactly n frames (caller checks available()). */
    void read (float* l, float* r, int n) noexcept
    {
        const auto rd = readPos.load (std::memory_order_relaxed);

        for (int i = 0; i < n; ++i)
        {
            const auto idx = (size_t) ((rd + (uint64_t) i) & (uint64_t) mask);
            if (l != nullptr) l[i] = left[idx];
            if (r != nullptr) r[i] = right[idx];
        }

        readPos.store (rd + (uint64_t) n, std::memory_order_release);
    }

    /** Consumer side: throw away n frames (or everything if n < 0). */
    void discard (int n = -1) noexcept
    {
        const auto avail = available();
        const auto count = (n < 0 || n > avail) ? avail : n;
        readPos.store (readPos.load (std::memory_order_relaxed) + (uint64_t) count, std::memory_order_release);
    }

private:
    static uint32_t nextPow2 (uint32_t v)
    {
        uint32_t p = 1;
        while (p < v) p <<= 1;
        return p;
    }

    std::vector<float> left, right;
    int capacity = 0, mask = 0;
    std::atomic<uint64_t> writePos { 0 }, readPos { 0 };
};

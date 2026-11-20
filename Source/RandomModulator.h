#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Independent deterministic streams; no allocation or host block-size dependency.
class RandomModulator
{
public:
    void prepare(double rate, uint32_t seed)
    {
        sampleRate = rate;
        random = seed;
        elapsed = 0;
        interval = 0;
        current = target = 0.5f;
    }

    float process(int samples, double hz, float smooth, float jitter, float chaos)
    {
        const double step = hz / sampleRate;
        const float coefficient = smooth <= 0 ? 1.0f
            : static_cast<float>(1 - std::exp(-step * 6 / smooth));
        for (int i = 0; i < samples; ++i)
        {
            if (elapsed >= interval)
            {
                elapsed -= interval;
                const float next = uniform();
                target = std::clamp(target + (next - target) * chaos, 0.0f, 1.0f);
                interval = 1.0 + (2 * uniform() - 1) * jitter * 0.95;
            }
            current += coefficient * (target - current);
            elapsed += step;
        }
        return current;
    }

private:
    float uniform()
    {
        random = random * 1664525u + 1013904223u;
        return static_cast<float>(random >> 8) / 16777216.0f;
    }
    double sampleRate = 44100, elapsed = 0, interval = 0;
    uint32_t random = 1234567;
    float current = 0.5f, target = 0.5f;
};

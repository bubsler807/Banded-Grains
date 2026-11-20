#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// DSP only: no JUCE dependencies. All memory is allocated in prepare().
class GranularEngine
{
public:
    struct Playback { float positionMs = 150, sprayMs = 200, pitch = 0, reverse = 0, spread = 0; bool freeze = false; };
    struct GrainVisual { float position = 0, progress = 0, amplitude = 0, speed = 0; };
    struct VisualState { std::array<GrainVisual, 32> grains {}; float recordHead = 0; };
    VisualState visualState() const
    {
        VisualState state;
        if (bufferSize == 0) return state;
        state.recordHead = static_cast<float>(writePosition) / bufferSize;
        for (size_t i = 0; i < grains.size(); ++i)
        {
            const auto& grain = grains[i];
            if (grain.length == 0) continue;
            const float phase = static_cast<float>(grain.age) / (grain.length - 1);
            state.grains[i] = {static_cast<float>(grain.position / bufferSize), phase,
                envelope(phase, grain.fade, grain.release), grain.speed};
        }
        return state;
    }
    struct PitchChoices { std::array<float, 4> semitones {}, chances {}; };
    static float choosePitch(const PitchChoices& choices, float random)
    {
        float total = 0;
        for (float chance : choices.chances) total += std::clamp(chance, 0.0f, 100.0f);
        float selection = random * std::max(100.0f, total);
        for (int i = 0; i < 4; ++i)
        {
            selection -= std::clamp(choices.chances[i], 0.0f, 100.0f);
            if (selection < 0) return std::clamp(choices.semitones[i], -24.0f, 24.0f);
        }
        return 0;
    }
    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        bufferSize = static_cast<int>(std::ceil(sampleRate * 4.0));
        for (auto& channel : history)
            channel.assign(static_cast<size_t>(bufferSize), 0.0f);
        for (auto& channel : feedbackHistory)
            channel.assign(static_cast<size_t>(bufferSize + 2), 0.0f);
        feedbackPosition = 0;
        dampingState = {};
        smoothedFeedback = 0;
        smoothedDelay = static_cast<float>(sampleRate * 0.35);
        smoothedDamping = static_cast<float>(1.0 - std::exp(-6.28318530718 * 6000 / sampleRate));
        grains = {};
        writePosition = 0;
        samplesUntilGrain = 0.0;
        smoothedMix = 0.0f;
        smoothedGain = 1.0f;
        mixStep = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.02)));
        randomState = 1234567;
    }

    void process(float* const* channels, int channelCount, int sampleCount,
                 float grainMilliseconds, float density, float mix, float shape = 0.0f, float releaseShape = -1.0f,
                 const PitchChoices* pitches = nullptr, float feedback = 0.0f,
                 float repeatMilliseconds = 350.0f, float dampingHz = 6000.0f, const Playback* playback = nullptr)
    {
        if (bufferSize == 0) return;
        const Playback settings = playback != nullptr ? *playback : Playback {};
        const int length = std::clamp(static_cast<int>(sampleRate * grainMilliseconds * 0.001),
                                      2, static_cast<int>(sampleRate * 0.25));
        const double interval = sampleRate / std::clamp(density, 1.0f, 60.0f);
        mix = std::clamp(mix, 0.0f, 1.0f);
        const float fade = 0.5f - 0.475f * std::clamp(shape, 0.0f, 1.0f);
        const float release = 0.5f - 0.475f * std::clamp(releaseShape < 0 ? shape : releaseShape, 0.0f, 1.0f);
        const float gain = 1.0f / std::max(1.0f, density * grainMilliseconds * 0.001f * (1.0f - (fade + release) * 0.5f));
        feedback = std::clamp(feedback, 0.0f, 0.95f);
        const float targetDelay = static_cast<float>(sampleRate * std::clamp(repeatMilliseconds, 10.0f, 2000.0f) * 0.001);
        const float damping = static_cast<float>(1.0 - std::exp(-6.28318530718
            * std::clamp(dampingHz, 100.0f, static_cast<float>(sampleRate * 0.45)) / sampleRate));
        const int feedbackSize = static_cast<int>(feedbackHistory[0].size());

        for (int sample = 0; sample < sampleCount; ++sample)
        {
            if (!settings.freeze)
                for (int ch = 0; ch < channelCount; ++ch)
                    history[ch][writePosition] = channels[ch][sample];

            if (samplesUntilGrain <= 0.0)
            {
                // Snapshot grain settings at launch. Changes affect new grains only.
                for (auto& grain : grains)
                    if (grain.length == 0)
                    {
                        randomState = randomState * 1664525u + 1013904223u;
                        const float random = static_cast<float>(randomState >> 8) / 16777216.0f;
                        float pitch = 0;
                        if (pitches != nullptr)
                        {
                            randomState = randomState * 1664525u + 1013904223u;
                            pitch = choosePitch(*pitches, static_cast<float>(randomState >> 8) / 16777216.0f);
                        }
                        pitch = std::clamp(pitch + settings.pitch, -36.0f, 36.0f);
                        float speed = std::pow(2.0f, pitch / 12.0f);
                        auto nextRandom = [&]() { randomState = randomState * 1664525u + 1013904223u;
                            return static_cast<float>(randomState >> 8) / 16777216.0f; };
                        if (settings.reverse > 0 && nextRandom() < std::clamp(settings.reverse, 0.0f, 1.0f)) speed = -speed;
                        const float pan = settings.spread > 0 ? (2 * nextRandom() - 1) * std::clamp(settings.spread, 0.0f, 1.0f) : 0;
                        const int travel = static_cast<int>(std::ceil(length * std::abs(speed)));
                        const int safeDelay = static_cast<int>(length * std::max(1.0f, std::abs(speed)));
                        const float offset = std::max(0.0f, std::clamp(settings.positionMs, 0.0f, 1500.0f)
                            + std::clamp(settings.sprayMs, 0.0f, 1000.0f) * (random - 0.5f));
                        const int maximumDelay = bufferSize - length - (speed < 0 ? travel : 0) - 2;
                        const int delay = std::clamp(safeDelay + static_cast<int>(sampleRate * offset * 0.001), 2, maximumDelay);
                        grain = { static_cast<double>((writePosition - delay + bufferSize) % bufferSize), 0, length, fade, release, speed,
                            std::sqrt(1 - pan), std::sqrt(1 + pan) };
                        break;
                    }
                samplesUntilGrain += interval;
            }
            --samplesUntilGrain;

            std::array<float, 2> wet {};
            for (auto& grain : grains)
                if (grain.length != 0)
                {
                    // Independent click-free cosine fades, captured at grain launch.
                    const float phase = static_cast<float>(grain.age) / static_cast<float>(grain.length - 1);
                    const float window = envelope(phase, grain.fade, grain.release);
                    const int lo = static_cast<int>(grain.position), hi = (lo + 1) % bufferSize;
                    const float fraction = static_cast<float>(grain.position - lo);
                    for (int ch = 0; ch < channelCount; ++ch)
                        wet[ch] += (history[ch][lo] + fraction * (history[ch][hi] - history[ch][lo])) * window
                            * (channelCount == 1 ? 1 : ch == 0 ? grain.leftGain : grain.rightGain);
                    grain.position += grain.speed;
                    if (grain.position >= bufferSize) grain.position -= bufferSize;
                    if (grain.position < 0) grain.position += bufferSize;
                    if (++grain.age >= grain.length) grain.length = 0;
                }

            // Conservative gain compensation for overlapping grains; not a limiter.
            smoothedGain += mixStep * (gain - smoothedGain);
            smoothedMix += mixStep * (mix - smoothedMix);
            smoothedFeedback += mixStep * (feedback - smoothedFeedback);
            smoothedDelay += mixStep * (targetDelay - smoothedDelay);
            smoothedDamping += mixStep * (damping - smoothedDamping);
            double readPosition = feedbackPosition - static_cast<double>(smoothedDelay);
            if (readPosition < 0) readPosition += feedbackSize;
            const int delayLo = static_cast<int>(readPosition), delayHi = (delayLo + 1) % feedbackSize;
            const float delayFraction = static_cast<float>(readPosition - delayLo);
            for (int ch = 0; ch < channelCount; ++ch)
            {
                const auto& line = feedbackHistory[ch];
                const float delayed = line[delayLo] + delayFraction * (line[delayHi] - line[delayLo]);
                // Feedback is taken before the output mix and re-enters the grain buffer.
                if (!settings.freeze) history[ch][writePosition] = channels[ch][sample] + smoothedFeedback * delayed;
                const float boundedWet = 2.0f * std::tanh(wet[ch] * smoothedGain * 0.5f);
                dampingState[ch] += smoothedDamping * (boundedWet - dampingState[ch]);
                feedbackHistory[ch][feedbackPosition] = dampingState[ch];
                channels[ch][sample] = channels[ch][sample] * (1.0f - smoothedMix)
                                    + wet[ch] * smoothedGain * smoothedMix;
            }
            feedbackPosition = (feedbackPosition + 1) % feedbackSize;
            if (!settings.freeze) writePosition = (writePosition + 1) % bufferSize;
        }
    }

    static float envelope(float phase, float attack, float release)
    {
        if (phase < attack) return 0.5f - 0.5f * std::cos(3.14159265359f * phase / attack);
        if (phase > 1.0f - release) return 0.5f - 0.5f * std::cos(3.14159265359f * (1.0f - phase) / release);
        return 1.0f;
    }
private:
    struct Grain { double position = 0; int age = 0, length = 0; float fade = 0.5f, release = 0.5f, speed = 1, leftGain = 1, rightGain = 1; };
    std::array<Grain, 32> grains {};
    std::array<std::vector<float>, 2> history;
    std::array<std::vector<float>, 2> feedbackHistory;
    std::array<float, 2> dampingState {};
    int feedbackPosition = 0;
    float smoothedFeedback = 0, smoothedDelay = 0, smoothedDamping = 0;
    double sampleRate = 44100.0, samplesUntilGrain = 0.0;
    int bufferSize = 0, writePosition = 0;
    float smoothedMix = 0.0f, smoothedGain = 1.0f, mixStep = 0.0f;
    uint32_t randomState = 1234567;
};

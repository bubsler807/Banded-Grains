#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// One insert in an ordered lane. All delay memory is allocated in prepare().
class EffectSlot
{
public:
    enum Type { distortion = 0, reverb = 1, equalizer = 2 };
    struct Settings
    {
        int type = distortion;
        bool enabled = true;
        float driveDb = 12, mix = 1, room = 0.5f, damping = 0.5f, width = 1;
        float lowDb = 0, midDb = 0, highDb = 0;
    };
    static constexpr int capacity = 256;

    void prepare(double rate)
    {
        sampleRate = rate;
        roomReverb.setSampleRate(rate);
        drive.reset(rate, 0.01); mix.reset(rate, 0.01);
        for (auto& gain : eqGains) gain.reset(rate, 0.02);
        reset();
    }
    void reset()
    {
        roomReverb.reset();
        for (auto& filter : filters) filter.reset();
        initialised = false;
    }
    void process(float* left, float* right, int samples, const Settings& settings)
    {
        jassert(samples <= capacity);
        if (lastType != settings.type) { reset(); lastType = settings.type; }
        const auto set = [this](juce::SmoothedValue<float>& value, float target)
        {
            if (initialised) value.setTargetValue(target);
            else value.setCurrentAndTargetValue(target);
        };
        set(drive, juce::Decibels::decibelsToGain(settings.driveDb));
        set(mix, settings.enabled ? settings.mix : 0);
        set(eqGains[0], settings.lowDb); set(eqGains[1], settings.midDb); set(eqGains[2], settings.highDb);
        initialised = true;
        if (!settings.enabled && !mix.isSmoothing() && mix.getCurrentValue() == 0)
        {
            if (active) { roomReverb.reset(); for (auto& filter : filters) filter.reset(); }
            active = false;
            return;
        }
        active = true;
        for (int i = 0; i < samples; ++i)
        {
            dry[0][i] = left[i];
            if (right != nullptr) dry[1][i] = right[i];
        }
        if (settings.type == distortion)
        {
            for (int i = 0; i < samples; ++i)
            {
                const float g = drive.getNextValue(), normalise = 1 / std::tanh(g);
                left[i] = std::tanh(left[i] * g) * normalise;
                if (right != nullptr) right[i] = std::tanh(right[i] * g) * normalise;
            }
        }
        else if (settings.type == reverb)
        {
            juce::Reverb::Parameters parameters;
            parameters.roomSize = settings.room; parameters.damping = settings.damping;
            parameters.width = settings.width;
            parameters.wetLevel = 1.0f / 3; parameters.dryLevel = 0;
            roomReverb.setParameters(parameters);
            if (!settings.enabled)
            {
                std::fill_n(left, samples, 0.0f);
                if (right != nullptr) std::fill_n(right, samples, 0.0f);
            }
            if (right != nullptr) roomReverb.processStereo(left, right, samples);
            else roomReverb.processMono(left, samples);
        }
        else
        {
            // Coefficients follow smoothed dB values in short sub-blocks; no allocation.
            for (int offset = 0; offset < samples; offset += 16)
            {
                const int length = juce::jmin(16, samples - offset);
                for (int band = 0; band < 3; ++band) filters[band].configure(band, eqGains[band].skip(length), sampleRate);
                for (int i = offset; i < offset + length; ++i)
                    for (int band = 0; band < 3; ++band)
                    {
                        left[i] = filters[band].process(0, left[i]);
                        if (right != nullptr) right[i] = filters[band].process(1, right[i]);
                    }
            }
        }
        for (int i = 0; i < samples; ++i)
        {
            const float amount = mix.getNextValue();
            left[i] = dry[0][i] + amount * (left[i] - dry[0][i]);
            if (right != nullptr) right[i] = dry[1][i] + amount * (right[i] - dry[1][i]);
        }
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 2> z1 {}, z2 {};
        void reset() { z1.fill(0); z2.fill(0); }
        float process(int channel, float input)
        {
            const double output = b0 * input + z1[channel];
            z1[channel] = b1 * input - a1 * output + z2[channel];
            z2[channel] = b2 * input - a2 * output;
            return static_cast<float>(output);
        }
        void configure(int band, float db, double rate)
        {
            const double frequency = std::array<double, 3>{250, 1000, 4000}[band];
            const double omega = juce::MathConstants<double>::twoPi * juce::jmin(frequency, rate * 0.45) / rate;
            const double c = std::cos(omega), sine = std::sin(omega), A = std::pow(10.0, db / 40.0);
            double a0;
            if (band == 1)
            {
                const double alpha = sine / (2 * 0.7071067811865476);
                b0 = 1 + alpha * A; b1 = -2 * c; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * c; a2 = 1 - alpha / A;
            }
            else
            {
                const double beta = std::sqrt(2 * A) * sine;
                if (band == 0)
                {
                    b0 = A * ((A + 1) - (A - 1) * c + beta);
                    b1 = 2 * A * ((A - 1) - (A + 1) * c);
                    b2 = A * ((A + 1) - (A - 1) * c - beta);
                    a0 = (A + 1) + (A - 1) * c + beta;
                    a1 = -2 * ((A - 1) + (A + 1) * c);
                    a2 = (A + 1) + (A - 1) * c - beta;
                }
                else
                {
                    b0 = A * ((A + 1) + (A - 1) * c + beta);
                    b1 = -2 * A * ((A - 1) + (A + 1) * c);
                    b2 = A * ((A + 1) + (A - 1) * c - beta);
                    a0 = (A + 1) - (A - 1) * c + beta;
                    a1 = 2 * ((A - 1) - (A + 1) * c);
                    a2 = (A + 1) - (A - 1) * c - beta;
                }
            }
            b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
        }
    };
    double sampleRate = 44100;
    juce::Reverb roomReverb;
    juce::SmoothedValue<float> drive, mix;
    std::array<juce::SmoothedValue<float>, 3> eqGains;
    std::array<Biquad, 3> filters;
    std::array<std::array<float, capacity>, 2> dry {};
    int lastType = -1;
    bool initialised = false, active = false;
};

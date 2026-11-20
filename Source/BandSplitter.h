#pragma once
#include <juce_dsp/juce_dsp.h>

// Compensate earlier bands for downstream crossover phase. Their sum has flat magnitude.
class BandSplitter
{
public:
    static constexpr int maxBands = 12, maxSplits = maxBands - 1;
    void prepare(double rate)
    {
        sampleRate = rate;
        const juce::dsp::ProcessSpec spec {rate, 256, 2};
        for (auto& filter : splits) filter.prepare(spec);
        for (auto& row : compensation)
            for (auto& filter : row)
            {
                filter.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
                filter.prepare(spec);
            }
        for (auto& cutoff : cutoffs) { cutoff.reset(rate, 0.03); cutoff.setCurrentAndTargetValue(1000); }
        activeBands = 0;
    }
    void configure(int count, std::array<float, maxSplits> frequencies)
    {
        count = juce::jlimit(1, maxBands, count);
        const bool changed = count != activeBands;
        activeBands = count;
        std::sort(frequencies.begin(), frequencies.begin() + count - 1);
        for (int i = 0; i < count - 1; ++i)
        {
            const auto f = juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.45), frequencies[i]);
            if (changed) cutoffs[i].setCurrentAndTargetValue(f);
            else cutoffs[i].setTargetValue(f);
        }
        if (changed)
        {
            for (auto& filter : splits) filter.reset();
            for (auto& row : compensation) for (auto& filter : row) filter.reset();
        }
    }
    void advance()
    {
        for (int i = 0; i < activeBands - 1; ++i)
        {
            const auto f = cutoffs[i].getNextValue();
            splits[i].setCutoffFrequency(f);
            for (int previous = 0; previous < i; ++previous)
                compensation[previous][i].setCutoffFrequency(f);
        }
    }
    std::array<float, maxBands> processSample(int channel, float input)
    {
        std::array<float, maxBands> bands {};
        float remainder = input;
        for (int split = 0; split < activeBands - 1; ++split)
            splits[split].processSample(channel, remainder, bands[split], remainder);
        bands[activeBands - 1] = remainder;
        for (int band = 0; band < activeBands - 1; ++band)
            for (int split = band + 1; split < activeBands - 1; ++split)
                bands[band] = compensation[band][split].processSample(channel, bands[band]);
        return bands;
    }
private:
    std::array<juce::dsp::LinkwitzRileyFilter<float>, maxSplits> splits;
    std::array<std::array<juce::dsp::LinkwitzRileyFilter<float>, maxSplits>, maxSplits> compensation;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, maxSplits> cutoffs;
    double sampleRate = 44100;
    int activeBands = 1;
};

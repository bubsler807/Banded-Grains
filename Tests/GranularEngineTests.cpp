#include "../Source/GranularEngine.h"
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }

int main()
{
    auto feedbackRender = [](float amount, float delay, float cutoff, bool dryInitially)
    {
        GranularEngine engine; engine.prepare(48000);
        std::vector<float> result(48000 * 8);
        for (int block = 0; block < 1500; ++block)
        {
            float audio[256]; float* channels[] {audio};
            for (int i = 0; i < 256; ++i)
            {
                const int sample = block * 256 + i;
                audio[i] = sample < 48000 ? 0.2f * std::sin(6.28318530718 * 4000 * sample / 48000) : 0.0f;
            }
            engine.process(channels, 1, 256, 100, 30, dryInitially && block < 375 ? 0.0f : 1.0f,
                0.7f, 0.4f, nullptr, amount, delay, cutoff);
            std::copy_n(audio, 256, result.begin() + block * 256);
        }
        return result;
    };
    const auto noFeedback = feedbackRender(0, 100, 20000, false);
    const auto shortFeedback = feedbackRender(0.8f, 100, 20000, false);
    const auto longFeedback = feedbackRender(0.8f, 800, 20000, false);
    const auto dampedFeedback = feedbackRender(0.8f, 100, 200, false);
    const auto initiallyDry = feedbackRender(0.8f, 100, 20000, true);
    double plainTail = 0, repeatTail = 0, darkTail = 0, endTail = 0;
    int firstShort = -1, firstLong = -1;
    for (size_t i = 0; i < noFeedback.size(); ++i)
    {
        if (firstShort < 0 && std::abs(shortFeedback[i] - noFeedback[i]) > 0.000001f) firstShort = static_cast<int>(i);
        if (firstLong < 0 && std::abs(longFeedback[i] - noFeedback[i]) > 0.000001f) firstLong = static_cast<int>(i);
        if (i > 48000 * 3 && i < 48000 * 4)
        {
            plainTail += noFeedback[i] * noFeedback[i];
            repeatTail += shortFeedback[i] * shortFeedback[i];
            darkTail += dampedFeedback[i] * dampedFeedback[i];
        }
        if (i > 48000 * 7) endTail += shortFeedback[i] * shortFeedback[i];
        if (i > 48000 * 3) require(std::abs(shortFeedback[i] - initiallyDry[i]) < 0.00001f, "Dry/wet changed feedback loop");
    }
    require(plainTail == 0 && repeatTail > 0.000001, "Feedback did not produce repeats after input stopped");
    require(firstLong > firstShort + 48000 / 3, "Repeat time did not delay feedback return");
    require(darkTail < repeatTail * 0.1, "Damping did not darken feedback repeats");
    require(endTail < repeatTail, "Feedback tail did not decay");
    {
        GranularEngine engine; engine.prepare(48000);
        GranularEngine::Playback playback;
        playback.positionMs = 600; playback.sprayMs = 0;
        float left[256], right[256]; float* channels[] {left, right};
        for (int block = 0; block < 200; ++block)
        {
            std::fill_n(left, 256, 0.2f); std::fill_n(right, 256, 0.2f);
            engine.process(channels, 2, 256, 100, 20, 1, 0, 0, nullptr, 0, 350, 6000, &playback);
        }
        const auto positioned = engine.visualState();
        int active = 0;
        for (const auto& grain : positioned.grains) if (grain.speed != 0)
        {
            const float age = std::fmod(positioned.recordHead - grain.position + 1, 1.0f) * 4;
            require(std::abs(age - 0.7f) < 0.001f, "Position did not set grain capture lookback"); ++active;
        }
        require(active > 0, "Grain visual state contained no active voices");
        playback.positionMs = 150; playback.sprayMs = 200; playback.pitch = 12; playback.reverse = 1; playback.spread = 1;
        double stereoDifference = 0;
        for (int block = 0; block < 100; ++block)
        {
            std::fill_n(left, 256, 0.2f); std::fill_n(right, 256, 0.2f);
            engine.process(channels, 2, 256, 100, 20, 1, 0, 0, nullptr, 0, 350, 6000, &playback);
            for (int i = 0; i < 256; ++i) stereoDifference += std::abs(left[i] - right[i]);
        }
        require(stereoDifference > 1, "Stereo spread did not distribute grains");
        for (const auto& grain : engine.visualState().grains) if (grain.speed != 0)
            require(grain.speed == -2, "Reverse or base pitch did not affect grain playback");
        playback.freeze = true;
        const float frozenHead = engine.visualState().recordHead;
        double frozenEnergy = 0;
        for (int block = 0; block < 1200; ++block)
        {
            std::fill_n(left, 256, 0.0f); std::fill_n(right, 256, 0.0f);
            engine.process(channels, 2, 256, 100, 20, 1, 0, 0, nullptr, 0, 350, 6000, &playback);
            if (block > 1000) for (float value : left) frozenEnergy += value * value;
        }
        require(engine.visualState().recordHead == frozenHead && frozenEnergy > 0.01, "Freeze did not retain captured audio");
        playback.freeze = false;
        for (int block = 0; block < 1000; ++block)
        {
            std::fill_n(left, 256, 0.0f); std::fill_n(right, 256, 0.0f);
            engine.process(channels, 2, 256, 100, 20, 1, 0, 0, nullptr, 0, 350, 6000, &playback);
        }
        for (float value : left) require(value == 0, "Unfreezing did not resume buffer recording");
    }
    require(GranularEngine::envelope(0, 0.025f, 0.5f) == 0, "Attack starts with a click");
    require(GranularEngine::envelope(1, 0.025f, 0.5f) == 0, "Release ends with a click");
    require(GranularEngine::envelope(0.1f, 0.025f, 0.5f) == 1, "Short attack not independent");
    require(GranularEngine::envelope(0.9f, 0.025f, 0.5f) < 0.1f, "Long release not independent");
    GranularEngine::PitchChoices choices {{-12, 7, 12, 24}, {20, 30, 10, 0}};
    std::array<int, 4> counts {};
    for (int i = 0; i < 10000; ++i)
    {
        const float pitch = GranularEngine::choosePitch(choices, (i + 0.5f) / 10000.0f);
        ++counts[pitch == -12 ? 0 : pitch == 7 ? 1 : pitch == 12 ? 2 : 3];
    }
    require(counts == std::array<int, 4>{2000, 3000, 1000, 4000}, "Pitch percentages or original remainder incorrect");
    choices.chances = {100, 100, 0, 0};
    require(GranularEngine::choosePitch(choices, 0.25f) == -12 && GranularEngine::choosePitch(choices, 0.75f) == 7,
            "Overfull probabilities not normalized");
    // Confirm playback pitch in the actual wet signal, including octave extremes.
    for (float semitones : {-24.0f, -12.0f, 12.0f, 24.0f})
    {
        GranularEngine engine;
        engine.prepare(48000);
        GranularEngine::PitchChoices pitch {{semitones, 0, 0, 0}, {100, 0, 0, 0}};
        float audio[512]; float* channels[] {audio};
        double targetPower = 0, originalPower = 0;
        const double target = 1000 * std::pow(2.0, semitones / 12.0);
        for (int block = 0; block < 300; ++block)
        {
            for (int i = 0; i < 512; ++i) audio[i] = 0.2f * std::sin(6.28318530718 * 1000 * (block * 512 + i) / 48000);
            engine.process(channels, 1, 512, 250, 2, 1, 0.4f, 0.8f, &pitch);
            if (block < 150) continue;
            for (int tone = 0; tone < 2; ++tone)
            {
                double real = 0, imaginary = 0;
                for (int i = 0; i < 512; ++i)
                {
                    const double window = 0.5 - 0.5 * std::cos(6.28318530718 * i / 511);
                    const double phase = 6.28318530718 * (tone == 0 ? target : 1000) * i / 48000;
                    real += audio[i] * window * std::cos(phase);
                    imaginary += audio[i] * window * std::sin(phase);
                }
                (tone == 0 ? targetPower : originalPower) += real * real + imaginary * imaginary;
            }
        }
        require(targetPower > originalPower * 100, "Wet audio did not play at selected pitch");
    }
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int channelCount : { 1, 2 })
        {
            GranularEngine engine;
            engine.prepare(rate);
            float left[257] {}, right[257] {};
            float* channels[] { left, right };
            // A fully dry signal must be preserved exactly.
            for (int i = 0; i < 257; ++i) left[i] = right[i] = 0.25f;
            engine.process(channels, channelCount, 257, 100, 15, 0);
            for (float value : left) require(value == 0.25f, "Dry signal changed");
            double energy = 0.0;
            for (int block = 0; block < 2000; ++block)
            {
                for (int i = 0; i < 257; ++i) left[i] = right[i] = 0.25f;
                // Exercise automation extremes, overlapping grains, and buffer wrap.
                engine.process(channels, channelCount, 257,
                               block % 2 ? 20.0f : 250.0f, block % 2 ? 2.0f : 40.0f, 1.0f,
                               static_cast<float>(block % 3) * 0.5f,
                               static_cast<float>((block + 1) % 3) * 0.5f, &choices,
                               block % 3 ? 0.95f : 0.0f, block % 2 ? 10.0f : 2000.0f, block % 2 ? 100.0f : 20000.0f);
                for (int i = 0; i < 257; ++i)
                {
                    require(std::isfinite(left[i]) && std::abs(left[i]) < 4.0f, "Unstable output");
                    if (channelCount == 2) require(left[i] == right[i], "Stereo alignment changed");
                    energy += left[i] * left[i];
                }
            }
            require(energy > 1.0, "Wet output is silent");
            engine.prepare(rate);
            for (int block = 0; block < 100; ++block)
            {
                std::fill_n(left, 257, 0.0f); std::fill_n(right, 257, 0.0f);
                engine.process(channels, channelCount, 257, 100, 15, 1);
                for (float value : left) require(value == 0.0f, "Reset retained audio");
            }
        }
    std::cout << "Granular engine tests passed\n";
}

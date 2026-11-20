#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "GranularEngine.h"
#include "BandSplitter.h"

class GranularProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int maxBands = BandSplitter::maxBands, maxSplits = maxBands - 1;
    static constexpr int maxModulators = 4, maxConnections = 4, modTargetCount = 22;
    inline static constexpr std::array<const char*, modTargetCount> modTargets {
        "size", "density", "mix", "shape", "release", "feedback", "repeat", "damping",
        "pitch0", "pitch1", "pitch2", "pitch3", "chance0", "chance1", "chance2", "chance3",
        "position", "spray", "pitch", "reverse", "spread", "freeze"};
    using GrainVisuals = std::array<GranularEngine::VisualState, maxBands>;
    bool readGrainVisuals(GrainVisuals&);
    static juce::String connectionID(int slot, int connection, const juce::String& control)
    { return modID(slot, "link" + juce::String(connection + 1) + "_" + control); }
    static juce::String modID(int slot, const juce::String& control)
    { return "mod" + juce::String(slot + 1) + "_" + control; }
    GranularProcessor();
    void prepareToPlay(double rate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Banded Grains"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    static juce::String moduleID(int module, const juce::String& control)
    { return module == 0 ? control : "m" + juce::String(module + 1) + "_" + control; }
    float value(const juce::String& id) const { return parameters.getRawParameterValue(id)->load(); }
    int bandCount() const { return juce::jlimit(1, maxBands, juce::roundToInt(value("bands"))); }
    std::array<float, maxSplits> boundaries() const;
    void setValue(const juce::String& id, float newValue);
    void addSplit(float frequency);
    void removeSplit(int index);
    void moveSplit(int index, float frequency);
    float displayMaxFrequency() const { return juce::jmin(20000.0f, displayRate.load() * 0.45f); }
    float spectrumSampleRate() const { return displayRate.load(); }
    int readSpectrum(float* left, float* right, int maximum);
    int listenedBand() const { const int source = auditionSource.load(); return source < maxBands ? source : -1; }
    int soloedModule() const { const int source = auditionSource.load(); return source >= maxBands ? source - maxBands : -1; }
    float liveValue(int module, int target) const { return liveValues[module][target].load(); }
    float modulationOutput(int source) const { return modOutputs[source].load(); }
    void toggleBandListen(int band) { auditionSource.store(auditionSource.load() == band ? -1 : band); }
    void toggleModuleSolo(int module) { const int source = maxBands + module; auditionSource.store(auditionSource.load() == source ? -1 : source); }
    juce::AudioProcessorValueTreeState parameters;
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    struct ModuleParameters { std::atomic<float>* size; std::atomic<float>* density;
                              std::atomic<float>* mix; std::atomic<float>* shape; std::atomic<float>* release; };
    std::array<ModuleParameters, maxBands> controls;
    std::array<std::array<std::atomic<float>*, 3>, maxBands> feedbackControls;
    std::array<std::array<std::atomic<float>*, 6>, maxBands> playbackControls;
    struct ModParameters { std::atomic<float>* wave; std::atomic<float>* rate; std::atomic<float>* sync; std::atomic<float>* connections; };
    struct ModConnection { std::atomic<float>* module; std::atomic<float>* target; std::atomic<float>* minimum; std::atomic<float>* maximum; };
    std::array<std::array<ModConnection, maxConnections>, maxModulators> modConnections;
    struct ModState { double phase = 0; float held = 0.5f; uint32_t random = 1234567; };
    std::array<ModParameters, maxModulators> modControls;
    std::array<ModState, maxModulators> modStates;
    std::atomic<float>* modCount = nullptr;
    std::array<std::array<juce::RangedAudioParameter*, modTargetCount>, maxBands> modDestinations;
    std::array<std::array<std::atomic<float>, modTargetCount>, maxBands> liveValues {};
    std::array<std::atomic<float>, maxModulators> modOutputs {};
    std::array<std::array<std::atomic<float>*, 4>, maxBands> pitchControls, chanceControls;
    std::array<std::atomic<float>*, maxSplits> splitControls;
    std::array<std::atomic<float>*, maxBands> routeControls;
    std::atomic<float>* countControl = nullptr;
    std::array<GranularEngine, maxBands> engines;
    std::array<juce::AudioBuffer<float>, maxBands> moduleAudio;
    juce::AudioBuffer<float> listenAudio;
    std::array<juce::SmoothedValue<float>, maxBands> listenGains;
    juce::SmoothedValue<float> mainGain;
    std::atomic<int> auditionSource {-1};
    std::array<juce::SmoothedValue<float>, maxBands> moduleSoloGains;
    std::array<std::array<juce::SmoothedValue<float>, maxBands>, maxBands> sends;
    BandSplitter splitter;
    static constexpr int chunkSize = 256, fifoSize = 16384;
    juce::AbstractFifo spectrumFifo {fifoSize};
    std::array<std::array<float, fifoSize>, 2> spectrumData {};
    std::atomic<float> displayRate {44100.0f};
    juce::AbstractFifo grainVisualFifo {8};
    std::array<GrainVisuals, 8> grainVisualData;
};

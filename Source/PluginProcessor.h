#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "GranularEngine.h"
#include "BandSplitter.h"
#include "RandomModulator.h"
#include "EffectsLane.h"

class GranularProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int maxBands = BandSplitter::maxBands, maxSplits = maxBands - 1;
    static constexpr int maxModulators = 4, maxConnections = 4, modTargetCount = 134;
    inline static constexpr std::array<const char*, modTargetCount> modTargets {
        "size", "density", "mix", "shape", "release", "feedback", "repeat", "damping",
        "pitch0", "pitch1", "pitch2", "pitch3", "chance0", "chance1", "chance2", "chance3",
        "position", "spray", "pitch", "reverse", "spread", "freeze",
        "fraction1_drive", "fraction1_distMix", "fraction1_reverb", "fraction1_room", "fraction1_damp", "fraction1_low", "fraction1_mid", "fraction1_high",
        "fraction2_drive", "fraction2_distMix", "fraction2_reverb", "fraction2_room", "fraction2_damp", "fraction2_low", "fraction2_mid", "fraction2_high",
        "fraction3_drive", "fraction3_distMix", "fraction3_reverb", "fraction3_room", "fraction3_damp", "fraction3_low", "fraction3_mid", "fraction3_high",
        "fraction4_drive", "fraction4_distMix", "fraction4_reverb", "fraction4_room", "fraction4_damp", "fraction4_low", "fraction4_mid", "fraction4_high",
        "pitch4", "chance4", "fraction5_drive", "fraction5_distMix", "fraction5_reverb", "fraction5_room", "fraction5_damp", "fraction5_low", "fraction5_mid", "fraction5_high",
        "pitch5", "chance5", "fraction6_drive", "fraction6_distMix", "fraction6_reverb", "fraction6_room", "fraction6_damp", "fraction6_low", "fraction6_mid", "fraction6_high",
        "pitch6", "chance6", "fraction7_drive", "fraction7_distMix", "fraction7_reverb", "fraction7_room", "fraction7_damp", "fraction7_low", "fraction7_mid", "fraction7_high",
        "pitch7", "chance7", "fraction8_drive", "fraction8_distMix", "fraction8_reverb", "fraction8_room", "fraction8_damp", "fraction8_low", "fraction8_mid", "fraction8_high",
        "pitch8", "chance8", "fraction9_drive", "fraction9_distMix", "fraction9_reverb", "fraction9_room", "fraction9_damp", "fraction9_low", "fraction9_mid", "fraction9_high",
        "pitch9", "chance9", "fraction10_drive", "fraction10_distMix", "fraction10_reverb", "fraction10_room", "fraction10_damp", "fraction10_low", "fraction10_mid", "fraction10_high",
        "pitch10", "chance10", "fraction11_drive", "fraction11_distMix", "fraction11_reverb", "fraction11_room", "fraction11_damp", "fraction11_low", "fraction11_mid", "fraction11_high",
        "pitch11", "chance11", "fraction12_drive", "fraction12_distMix", "fraction12_reverb", "fraction12_room", "fraction12_damp", "fraction12_low", "fraction12_mid", "fraction12_high"};
    static constexpr int fractionCount = 12, fractionControlCount = 8;
    inline static constexpr std::array<const char*, fractionControlCount> fractionControls {"drive", "distMix", "reverb", "room", "damp", "low", "mid", "high"};
    static juce::String fractionID(int module, int group, const juce::String& control)
    { return moduleID(module, "fraction" + juce::String(group + 1) + "_" + control); }
    static int fractionTarget(int group, int control)
    { return group < 4 ? (control == 0 ? 8 + group : control == 1 ? 12 + group : 22 + group * 8 + control - 2) : 54 + (group - 4) * 10 + control; }
    static int targetFraction(int target)
    { return target >= 54 ? 4 + (target - 54) / 10 : target >= 22 ? (target - 22) / 8 : target >= 12 && target < 16 ? target - 12 : target >= 8 && target < 12 ? target - 8 : -1; }
    static int targetFractionControl(int target)
    { return target >= 54 ? (target - 54) % 10 : target >= 22 ? 2 + (target - 22) % 8 : target >= 12 && target < 16 ? 1 : 0; }
    int activeFractions(int module) const { return juce::jlimit(0, fractionCount, juce::roundToInt(value(moduleID(module, "fractions")))); }
    float availableFractionShare(int module, int group) const;
    void setFractionShare(int module, int group, float share);
    void addFraction(int module);
    void removeFraction(int module, int group);
    static constexpr int effectLaneCount = maxBands + 1, maxEffectsPerLane = 8;
    inline static constexpr std::array<const char*, 3> effectControls {"count", "mix", "gain"};
    inline static constexpr std::array<const char*, 10> effectSlotControls {
        "type", "enabled", "drive", "mix", "room", "damping", "width", "low", "mid", "high"};
    static juce::String effectID(int lane, const juce::String& control)
    { return (lane == maxBands ? juce::String("masterFx_") : "bandFx" + juce::String(lane + 1) + "_") + control; }
    static juce::String effectSlotID(int lane, int slot, const juce::String& control)
    { return effectID(lane, "slot" + juce::String(slot + 1) + "_" + control); }
    int effectCount(int lane) const { return juce::jlimit(0, maxEffectsPerLane, juce::roundToInt(value(effectID(lane, "count")))); }
    void addEffect(int lane, int type);
    void removeEffect(int lane, int slot);
    void moveEffect(int lane, int slot, int direction);
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
    struct ModParameters { std::atomic<float>* wave; std::atomic<float>* rate; std::atomic<float>* sync; std::atomic<float>* connections; std::atomic<float>* polarity; std::atomic<float>* type;
        std::atomic<float>* smooth; std::atomic<float>* jitter; std::atomic<float>* chaos; };
    struct ModConnection { std::atomic<float>* module; std::atomic<float>* target; std::atomic<float>* depth; };
    std::array<std::array<ModConnection, maxConnections>, maxModulators> modConnections;
    struct ModState { double phase = 0; float held = 0.5f; uint32_t random = 1234567; };
    std::array<ModParameters, maxModulators> modControls;
    std::array<ModState, maxModulators> modStates;
    std::array<RandomModulator, maxModulators> randomModulators;
    std::atomic<float>* modCount = nullptr;
    std::array<std::array<juce::RangedAudioParameter*, modTargetCount>, maxBands> modDestinations;
    std::array<std::array<std::atomic<float>, modTargetCount>, maxBands> liveValues {};
    std::array<std::atomic<float>, maxModulators> modOutputs {};
    std::array<std::array<std::atomic<float>*, fractionCount>, maxBands> pitchControls, chanceControls;
    std::array<std::atomic<float>*, maxSplits> splitControls;
    std::array<std::atomic<float>*, maxBands> routeControls;
    std::atomic<float>* countControl = nullptr;
    void processEffectLane(int lane, float* left, float* right, int samples);
    void processFractions(int module, int channels, int samples, const std::array<float, modTargetCount>& values);
    std::array<std::array<std::array<std::atomic<float>*, fractionControlCount>, fractionCount>, maxBands> fractionParameters;
    std::array<std::array<std::array<std::unique_ptr<EffectSlot>, 3>, fractionCount>, maxBands> fractionEffects;
    std::array<std::atomic<float>*, maxBands> fractionCounts;
    std::array<std::atomic<bool>, maxBands> resetFractionRouting {};
    GranularEngine::GroupOutput fractionAudio;
    std::array<std::array<float, 256>, 2> fractionScratch;

    void copyEffectLane(int destination, int source);
    std::array<std::array<std::atomic<float>*, 3>, effectLaneCount> effectParameters;
    std::array<std::array<std::array<std::atomic<float>*, 10>, maxEffectsPerLane>, effectLaneCount> effectSlotParameters;
    std::array<std::array<std::unique_ptr<EffectSlot>, maxEffectsPerLane>, effectLaneCount> effectSlots;
    std::array<std::atomic<unsigned>, effectLaneCount> resetEffectSlots {};
    std::array<juce::SmoothedValue<float>, effectLaneCount> effectMix, effectGain;
    std::array<juce::AudioBuffer<float>, maxBands> bandAudio;
    std::atomic<bool> resetEffects {false};
    std::array<GranularEngine, maxBands> engines;
    std::array<juce::AudioBuffer<float>, maxBands> moduleAudio;
    juce::AudioBuffer<float> listenAudio;
    std::array<juce::SmoothedValue<float>, maxBands> listenGains;
    juce::SmoothedValue<float> mainGain;
    std::atomic<int> auditionSource {-1};
    int processedBandCount = 1;
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

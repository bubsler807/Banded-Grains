#pragma once
#include "PluginProcessor.h"

class GranularEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GranularEditor(GranularProcessor&);
    ~GranularEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void paintOverChildren(juce::Graphics&) override;
private:
    class Spectrum;
    class PaperLook;
    class Envelope;
    class Socket;
    class GrainView;
    class EffectCard;
    class EffectChain;
    void showPlayback(bool);
    void showEffects(bool);
    void refreshEffects();
    void showEffectMenu();
    void updateModulatorType();
    void refreshModulation();
    void connectModulator(int source, int target);
    void removeConnection(int connection);
    void updateDepthText(int connection);
    void timerCallback() override;
    void selectModule(int);
    GranularProcessor& processor;
    std::unique_ptr<PaperLook> paperLook;
    std::unique_ptr<Spectrum> spectrum;
    std::unique_ptr<Envelope> envelope;
    std::unique_ptr<GrainView> grainView;
    juce::TextButton grainsPage {"Grains"}, playbackPage {"Playback"}, freezeButton {"Freeze buffer"};
    bool playbackVisible = false, effectsVisible = false;
    juce::TextButton effectsPage {"Effects"};
    juce::ComboBox effectLaneSelector;
    juce::Viewport effectViewport;
    std::unique_ptr<EffectChain> effectChain;
    juce::Slider effectLaneMix, effectLaneGain;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> effectMixAttachment, effectGainAttachment;
    int selectedEffectLane = GranularProcessor::maxBands;

    std::array<juce::Slider, 5> playbackSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> playbackAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;
    juce::Component bandStrip;
    juce::Viewport bandViewport;
    std::array<juce::Label, GranularProcessor::maxBands> bandLabels;
    int displayedBands = 0;
    std::array<juce::ComboBox, GranularProcessor::maxBands> routes;
    std::array<juce::TextButton, GranularProcessor::maxBands> listenButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, GranularProcessor::maxBands> routeAttachments;
    std::array<juce::TextButton, GranularProcessor::maxBands> tabs;
    std::array<juce::TextButton, GranularProcessor::maxBands> moduleSoloButtons;
    std::array<juce::Slider, 3> sliders;
    std::array<juce::Slider, 3> feedbackSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> feedbackAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> attachments;
    std::array<juce::Slider, 4> pitchSteps, pitchChances;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> stepAttachments, chanceAttachments;
    juce::ComboBox sourceSelector, modWave, modSync, modPolarity, modType;
    juce::Slider modRate;
    std::array<juce::Slider, 3> randomSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> randomAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
    juce::TextButton addModulator {"Add modulator"}, removeModulator {"Remove"};
    juce::Viewport connectionViewport;
    juce::Component connectionStrip;
    struct ConnectionRow
    {
        juce::ComboBox module, target;
        juce::Slider depth;
        juce::TextButton remove {"Remove"};
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> moduleAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment;
    };
    std::array<ConnectionRow, GranularProcessor::maxConnections> connectionRows;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveAttachment, syncAttachment, polarityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAttachment;
    std::array<std::unique_ptr<Socket>, GranularProcessor::modTargetCount> inputSockets;
    std::array<std::unique_ptr<Socket>, GranularProcessor::maxModulators> outputSockets;
    int selectedSource = 0, shownModCount = -1, shownConnections = -1;
    int draggingSource = -1, hoveredSource = -1, hoveredTarget = -1;
    juce::Point<float> wireEnd;
    juce::String patchMessage;
    int selectedModule = 0;
};

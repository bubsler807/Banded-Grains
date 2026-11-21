#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <cmath>
#include "../Source/BandSplitter.h"
#include "../Source/PluginProcessor.h"
#define NOMINMAX
#include <windows.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

void pumpMessages(int milliseconds)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(milliseconds);
    do
    {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        { TranslateMessage(&message); DispatchMessageW(&message); }
        juce::Thread::sleep(1);
    } while (juce::Time::getMillisecondCounter() < end);
}

void check(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }

void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }

juce::Component* findComponent(juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findComponent(*child, id)) return found;
    return nullptr;
}

void checkCrossovers()
{
    for (double rate : {44100.0, 48000.0, 96000.0})
        for (int bands = 1; bands <= GranularProcessor::maxBands; ++bands)
            for (float hz : {50.0f, 200.0f, 500.0f, 1000.0f, 3000.0f, 5000.0f, 10000.0f})
            {
                BandSplitter splitter;
                splitter.prepare(rate);
                std::array<float, GranularProcessor::maxSplits> frequencies {200, 1000, 5000};
                if (bands > 4)
                    for (int i = 0; i < bands - 1; ++i) frequencies[i] = 30.0f * std::pow(500.0f, static_cast<float>(i) / (bands - 2));
                splitter.configure(bands, frequencies);
                double inEnergy = 0, outEnergy = 0;
                std::array<double, GranularProcessor::maxBands> bandEnergy {};
                for (int i = 0; i < static_cast<int>(rate * 0.4); ++i)
                {
                    const float input = std::sin(static_cast<float>(2 * juce::MathConstants<double>::pi * hz * i / rate));
                    splitter.advance();
                    const auto split = splitter.processSample(0, input);
                    float output = 0;
                    for (int b = 0; b < bands; ++b) output += split[b];
                    if (i > rate * 0.2)
                    {
                        inEnergy += input * input; outEnergy += output * output;
                        for (int b = 0; b < bands; ++b) bandEnergy[b] += split[b] * split[b];
                    }
                }
                check(std::abs(std::sqrt(outEnergy / inEnergy) - 1.0) < 0.008, "Crossover recombination is not flat");
                if (bands > 1 && hz == 50) check(bandEnergy[0] > bandEnergy[bands - 1] * 100, "Low band isolation failed");
                if (bands > 1 && hz == 10000) check(bandEnergy[bands - 1] > bandEnergy[0] * 100, "High band isolation failed");
            }
    std::cout << "Crossover magnitude and band isolation passed at three sample rates\n";
}

void click(juce::Component& component, float frequency, int clicks, bool right = false, bool drag = false)
{
    const float x = 42 + (component.getWidth() - 84) * std::log(frequency / 20) / std::log(20000.0f / 20);
    const juce::Point<float> position(x, 100);
    const auto time = juce::Time::getCurrentTime();
    const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), position,
        juce::ModifierKeys(right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier),
        1, 0, 0, 0, 0, &component, &component, time, position, time, clicks, drag);
    if (drag) component.mouseDrag(event);
    else if (clicks == 2) component.mouseDoubleClick(event);
    else component.mouseDown(event);
}

void checkAutomatedBandLayout()
{
    GranularProcessor processor;
    processor.setValue("bands", 3);
    processor.setValue("split0", 6000);
    processor.setValue("split1", 200);
    processor.moveSplit(0, 300);
    check(processor.value("split0") == 6000 && processor.value("split1") == 300,
          "Dragging a reordered crossover changed the wrong parameter");
    processor.moveSplit(1, 7000);
    check(processor.value("split0") == 7000 && processor.value("split1") == 300,
          "Dragging the upper reordered crossover changed another divider");

    processor.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> audio(2, 256); juce::MidiBuffer midi;
    auto process = [&] { audio.clear(); processor.processBlock(audio, midi); };
    processor.toggleBandListen(0);
    process();
    check(processor.listenedBand() == 0, "Unchanged band layout cleared band audition");
    processor.setValue("bands", 4);
    process();
    check(processor.listenedBand() == -1, "Automated band insertion left audition active");
    processor.toggleModuleSolo(1);
    process();
    check(processor.soloedModule() == 1, "Unchanged band layout cleared module solo");
    processor.setValue("bands", 2);
    process();
    check(processor.soloedModule() == -1, "Automated band removal left module solo active");
}

void checkModulationContributions()
{
    GranularProcessor processor;
    processor.setValue("modCount", 2);
    processor.setValue("pitch", 7);
    for (int slot = 0; slot < 2; ++slot)
    {
        processor.setValue(GranularProcessor::modID(slot, "wave"), 4); // Square starts at zero.
        processor.setValue(GranularProcessor::modID(slot, "polarity"), 2); // Inverted output = 1.
        processor.setValue(GranularProcessor::modID(slot, "connections"), 1);
        processor.setValue(GranularProcessor::connectionID(slot, 0, "target"), 18);
    }
    processor.setValue("mod1_link1_depth", 25); // +12 semitones.
    processor.setValue("mod2_link1_depth", -12.5f); // -6 semitones.
    for (double rate : {44100.0, 48000.0, 96000.0})
    {
        processor.prepareToPlay(rate, 257);
        for (int length : {1, 64, 257})
        {
            juce::AudioBuffer<float> audio(2, length); audio.clear(); juce::MidiBuffer midi;
            processor.processBlock(audio, midi);
            check(std::abs(processor.liveValue(0, 18) - 13) < 0.06f, "Signed contributions did not sum in destination units");
        }
        processor.releaseResources();
    }
    processor.prepareToPlay(48000, 256);
    auto process = [&]
    {
        juce::AudioBuffer<float> audio(2, 256); audio.clear(); juce::MidiBuffer midi;
        processor.processBlock(audio, midi);
    };
    processor.setValue("mod1_link1_depth", 100);
    processor.setValue("mod2_link1_depth", -100);
    process();
    check(std::abs(processor.liveValue(0, 18) - 7) < 0.06f, "Contributions clipped before cancellation");
    processor.setValue("mod2_link1_depth", 0);
    process();
    check(processor.liveValue(0, 18) == 24, "Final modulation value not clamped");
    processor.setValue("mod1_link1_depth", 25);
    processor.setValue("mod1_polarity", 0); process();
    check(std::abs(processor.liveValue(0, 18) - 7) < 0.06f, "Unipolar zero changed base value");
    processor.setValue("mod1_polarity", 1); process();
    check(std::abs(processor.liveValue(0, 18) + 5) < 0.06f && processor.modulationOutput(0) == -1,
          "Bipolar output or depth incorrect");
    check(processor.value("pitch") == 7, "Contributions changed base automation value");
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    processor.setValue("mod1_link1_depth", -10); processor.setValue("mod1_polarity", 2);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    process();
    check(processor.value("mod1_link1_depth") == 25 && processor.value("mod1_polarity") == 1
          && std::abs(processor.liveValue(0, 18) + 5) < 0.06f, "Contribution patch recall failed");
    processor.releaseResources();
}

void checkRandomAndEffects(bool previews)
{
    RandomModulator stepped, smooth, split, independent, jittered, held;
    for (auto* random : {&stepped, &smooth, &split, &jittered, &held}) random->prepare(48000, 1234);
    independent.prepare(48000, 5678);
    float previous = 0.5f;
    bool differentStream = false, differentTiming = false, smoothingObserved = false;
    for (int i = 0; i < 40000; ++i)
    {
        const float a = stepped.process(1, 20, 0, 0, 1);
        const float b = smooth.process(1, 20, 1, 0, 1);
        const float c = independent.process(1, 20, 0, 0, 1);
        const float d = jittered.process(1, 20, 0, 1, 1);
        check(a >= 0 && a <= 1 && b >= 0 && b <= 1, "Random output outside unipolar range");
        check(held.process(1, 20, 0, 0, 0) == 0.5f, "Zero chaos did not hold target");
        if (i > 0) check(std::abs(b - previous) < 0.003f, "Random smoothing has a discontinuity");
        previous = b;
        differentStream |= a != c;
        differentTiming |= a != d;
        smoothingObserved |= std::abs(a - b) > 0.01f;
    }
    const float oneBlock = split.process(40000, 20, 0, 0, 1);
    check(oneBlock == stepped.process(0, 20, 0, 0, 1), "Random depends on host block size");
    check(differentStream && differentTiming && smoothingObserved, "Random controls have no effect");

    for (double rate : {44100.0, 48000.0, 96000.0})
        for (int channels : {1, 2})
        {
            EffectSlot effect;
            EffectSlot::Settings settings;
            effect.prepare(rate);
            std::array<float, 256> left, right;
            left.fill(0.1f); right.fill(-0.2f);
            settings.enabled = false;
            effect.process(left.data(), channels == 2 ? right.data() : nullptr, 256, settings);
            check(left[0] == 0.1f && (channels == 1 || right[0] == -0.2f), "Disabled effect changed dry audio");
            settings.enabled = true; effect.prepare(rate);
            left.fill(0.1f); right.fill(-0.2f);
            effect.process(left.data(), channels == 2 ? right.data() : nullptr, 256, settings);
            check(left[0] > 0.2f && std::isfinite(left[0]), "Distortion did not process audio");
            settings.mix = 0; effect.prepare(rate);
            left.fill(0.1f); right.fill(-0.2f);
            effect.process(left.data(), channels == 2 ? right.data() : nullptr, 256, settings);
            check(left[0] == 0.1f, "Effect zero mix changed dry audio");
            settings.mix = 1; settings.type = EffectSlot::reverb;
            effect.prepare(rate);
            double tailEnergy = 0;
            for (int block = 0; block < 200; ++block)
            {
                left.fill(0); right.fill(0);
                if (block == 0) left[0] = 1;
                effect.process(left.data(), channels == 2 ? right.data() : nullptr, 256, settings);
                for (int i = 0; i < 256; ++i)
                {
                    check(std::isfinite(left[i]) && std::abs(left[i]) < 2, "Invalid reverb output");
                    if (channels == 2) check(std::isfinite(right[i]), "Invalid stereo reverb output");
                    if (block > 5) tailEnergy += left[i] * left[i];
                }
            }
            check(tailEnergy > 0.00001, "Reverb tail is silent");
            settings.type = EffectSlot::equalizer;
            const auto eqEnergy = [&](float hz, float low, float mid, float high)
            {
                settings.lowDb = low; settings.midDb = mid; settings.highDb = high;
                effect.prepare(rate);
                double energy = 0;
                for (int block = 0; block < 80; ++block)
                {
                    for (int i = 0; i < 256; ++i)
                        left[i] = right[i] = 0.1f * std::sin(juce::MathConstants<float>::twoPi * hz * (block * 256 + i) / rate);
                    effect.process(left.data(), channels == 2 ? right.data() : nullptr, 256, settings);
                    for (int i = 0; i < 256; ++i)
                    {
                        check(std::isfinite(left[i]) && std::abs(left[i]) < 3, "EQ output is invalid");
                        if (channels == 2) check(std::abs(left[i] - right[i]) < 0.00001, "EQ stereo channels differ");
                        if (block > 5) energy += left[i] * left[i];
                    }
                }
                return energy;
            };
            const double lowDry = eqEnergy(50, 0, 0, 0), midDry = eqEnergy(1000, 0, 0, 0), highDry = eqEnergy(12000, 0, 0, 0);
            check(eqEnergy(50, 12, 0, 0) > lowDry * 10, "EQ low shelf did not boost bass");
            check(eqEnergy(1000, 0, -12, 0) < midDry * 0.1, "EQ mid cut did not affect centre frequency");
            check(eqEnergy(12000, 0, 0, 12) > highDry * 10, "EQ high shelf did not boost treble");
        }

    {
        GranularProcessor chain;
        const int master = GranularProcessor::maxBands;
        chain.setValue("mix", 0);
        chain.addEffect(master, EffectSlot::distortion); chain.addEffect(master, EffectSlot::distortion);
        chain.setValue("masterFx_slot2_drive", 6);
        chain.prepareToPlay(48000, 256);
        juce::AudioBuffer<float> audio(2, 256); juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) audio.setSample(ch, i, 0.1f);
        chain.processBlock(audio, midi);
        const auto distort = [](float input, float db)
        {
            const float g = juce::Decibels::decibelsToGain(db);
            return std::tanh(input * g) / std::tanh(g);
        };
        check(std::abs(audio.getSample(0, 255) - distort(distort(0.1f, 12), 6)) < 0.0001f,
              "Repeated effects were not processed serially");
        chain.moveEffect(master, 1, -1);
        chain.prepareToPlay(48000, 256);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) audio.setSample(ch, i, 0.1f);
        chain.processBlock(audio, midi);
        check(std::abs(audio.getSample(0, 255) - distort(distort(0.1f, 6), 12)) < 0.0001f,
              "Moving an effect did not change DSP order");
        chain.setValue("masterFx_slot2_enabled", 0);
        chain.prepareToPlay(48000, 256);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) audio.setSample(ch, i, 0.1f);
        chain.processBlock(audio, midi);
        check(std::abs(audio.getSample(0, 255) - distort(0.1f, 6)) < 0.0001f, "Insert bypass changed other effects");
        chain.setValue("masterFx_mix", 0);
        chain.prepareToPlay(48000, 256);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) audio.setSample(ch, i, 0.1f);
        chain.processBlock(audio, midi);
        check(std::abs(audio.getSample(0, 255) - 0.1f) < 0.0001f, "Lane zero mix did not bypass the chain");
        chain.releaseResources();
        for (int i = 0; i < GranularProcessor::maxEffectsPerLane + 1; ++i) chain.addEffect(11, EffectSlot::equalizer);
        check(chain.effectCount(11) == GranularProcessor::maxEffectsPerLane, "Effect lane capacity not bounded");
    }
    GranularProcessor processor;
    processor.setValue("bands", 2); processor.setValue("split0", 1000);
    processor.setValue("mix", 0); processor.setValue("m2_mix", 0);
    processor.addEffect(GranularProcessor::maxBands, EffectSlot::distortion);
    const auto energy = [&](float hz, bool bandAttenuation, bool masterDistortion)
    {
        processor.setValue("bandFx1_gain", bandAttenuation ? -24.0f : 0.0f);
        processor.setValue("masterFx_slot1_enabled", masterDistortion ? 1.0f : 0.0f);
        processor.prepareToPlay(48000, 256);
        double sum = 0;
        for (int block = 0; block < 80; ++block)
        {
            juce::AudioBuffer<float> audio(2, 256); juce::MidiBuffer midi;
            for (int i = 0; i < 256; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    audio.setSample(ch, i, 0.1f * std::sin(juce::MathConstants<float>::twoPi * hz * (block * 256 + i) / 48000));
            processor.processBlock(audio, midi);
            if (block > 10) for (int i = 0; i < 256; ++i) sum += audio.getSample(0, i) * audio.getSample(0, i);
        }
        processor.releaseResources();
        return sum;
    };
    const double lowDry = energy(80, false, false), highDry = energy(8000, false, false);
    check(energy(80, true, false) < lowDry * 0.01, "Band lane failed to isolate low band");
    check(energy(8000, true, false) > highDry * 0.95, "Low-band effect changed high band");
    check(energy(80, false, true) > lowDry * 5, "Master effects did not process combined output");
    processor.setValue("modCount", 1); processor.setValue("mod1_type", 1);
    processor.setValue("mod1_smooth", 60); processor.setValue("mod1_jitter", 25); processor.setValue("mod1_chaos", 70);
    processor.addEffect(1, EffectSlot::reverb);
    processor.addEffect(1, EffectSlot::equalizer);
    processor.setValue("bandFx2_slot2_mid", -6);
    processor.setValue("masterFx_slot1_room", 80);
    juce::MemoryBlock state; processor.getStateInformation(state);
    processor.setValue("mod1_type", 0); processor.setValue("bandFx2_slot1_enabled", 0); processor.setValue("masterFx_slot1_room", 20);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(processor.value("mod1_type") == 1 && processor.value("mod1_smooth") == 60
          && processor.value("mod1_jitter") == 25 && processor.value("mod1_chaos") == 70
          && processor.value("bandFx2_slot1_enabled") == 1 && processor.value("bandFx2_slot2_type") == EffectSlot::equalizer
          && processor.value("bandFx2_slot2_mid") == -6
          && processor.value("masterFx_slot1_room") == 80, "Random/effects patch recall failed");
    processor.addEffect(0, EffectSlot::distortion);
    processor.setValue("bandFx1_slot1_drive", 3); processor.setValue("bandFx2_slot1_drive", 19);
    processor.addSplit(200);
    check(processor.value("bandFx1_slot1_drive") == 3 && processor.value("bandFx2_slot1_drive") == 3
          && processor.value("bandFx3_slot1_drive") == 19, "Band insertion did not preserve effect lanes");
    processor.removeSplit(0);
    check(processor.value("bandFx2_slot1_drive") == 19, "Band removal did not preserve effect lanes");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* fxPage = dynamic_cast<juce::TextButton*>(findComponent(*editor, "effectsPage"));
    fxPage->onClick();
    auto* lane = dynamic_cast<juce::ComboBox*>(findComponent(*editor, "fxLane"));
    auto* viewport = dynamic_cast<juce::Viewport*>(findComponent(*editor, "effectViewport"));
    check(lane && viewport, "Effects lane or scroll viewport missing");
    lane->setSelectedId(1, juce::sendNotificationSync);
    auto* drive = dynamic_cast<juce::Slider*>(findComponent(*editor, "effect0_drive"));
    check(drive && drive->isVisible(), "Distortion card missing");
    drive->setValue(21, juce::sendNotificationSync);
    check(processor.value("bandFx1_slot1_drive") == 21, "Band effect control not attached");
    processor.addEffect(0, EffectSlot::reverb); processor.addEffect(0, EffectSlot::equalizer);
    pumpMessages(100);
    auto* mid = dynamic_cast<juce::Slider*>(findComponent(*editor, "effect2_mid"));
    check(mid, "Added EQ card missing"); mid->setValue(-9, juce::sendNotificationSync);
    check(processor.value("bandFx1_slot3_mid") == -9, "EQ control not attached");
    check(viewport->getViewedComponent()->getHeight() > viewport->getHeight(), "Effect list did not grow for scrolling");
    viewport->setViewPosition(0, 200);
    check(viewport->getViewPositionY() > 0, "Effect lane did not scroll");
    auto* up = dynamic_cast<juce::TextButton*>(findComponent(*editor, "effectUp2"));
    up->onClick(); pumpMessages(100);
    check(processor.value("bandFx1_slot2_type") == EffectSlot::equalizer && processor.value("bandFx1_slot2_mid") == -9,
          "Reordering effect lost its settings");
    auto* remove = dynamic_cast<juce::TextButton*>(findComponent(*editor, "effectRemove0"));
    remove->onClick(); pumpMessages(100);
    check(processor.effectCount(0) == 2 && processor.value("bandFx1_slot1_type") == EffectSlot::equalizer,
          "Removing an effect did not compact the lane");
    while (processor.effectCount(0) > 0) processor.removeEffect(0, 0);
    pumpMessages(100);
    auto* emptyLane = findComponent(*editor, "effectChain");
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent laneClick(juce::Desktop::getInstance().getMainMouseSource(), {20, 100},
        juce::ModifierKeys(), 1, 0, 0, 0, 0, emptyLane, emptyLane, now, {20, 100}, now, 1, false);
    emptyLane->mouseUp(laneClick);
    auto* emptyMenu = juce::Component::getCurrentlyModalComponent();
    check(emptyMenu, "Clicking empty lane space did not open the effect picker");
    emptyMenu->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    pumpMessages(100);
    check(processor.effectCount(0) == 1 && processor.value("bandFx1_slot1_type") == EffectSlot::distortion,
          "Choosing an effect in an empty lane failed");
    lane->setSelectedId(GranularProcessor::maxBands + 1, juce::sendNotificationSync);
    drive = dynamic_cast<juce::Slider*>(findComponent(*editor, "effect0_drive"));
    check(drive, "Master distortion card missing"); drive->setValue(7, juce::sendNotificationSync);
    check(processor.value("masterFx_slot1_drive") == 7, "Master insert UI not attached");
    processor.addEffect(GranularProcessor::maxBands, EffectSlot::reverb);
    auto* addEffect = dynamic_cast<juce::TextButton*>(findComponent(*editor, "addEffect"));
    check(addEffect, "Add effect button missing");
    addEffect->onClick();
    auto* menu = juce::Component::getCurrentlyModalComponent();
    check(menu, "Clicking the lane did not open an effect picker");
    menu->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    menu->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    menu->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    pumpMessages(100);
    check(processor.effectCount(GranularProcessor::maxBands) == 3
          && processor.value("masterFx_slot3_type") == EffectSlot::equalizer,
          "Choosing EQ from the picker did not append an EQ card");
    processor.getStateInformation(state);
    processor.removeEffect(GranularProcessor::maxBands, 0);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    pumpMessages(100);
    check(processor.effectCount(GranularProcessor::maxBands) == 3 && processor.value("masterFx_slot1_drive") == 7,
          "Ordered effect chain recall failed");
    auto* type = dynamic_cast<juce::ComboBox*>(findComponent(*editor, "modType"));
    auto* jitter = dynamic_cast<juce::Slider*>(findComponent(*editor, "randomJitter"));
    check(type && jitter && type->getSelectedId() == 2 && jitter->isVisible(), "Random type UI missing after recall");
    jitter->setValue(40, juce::sendNotificationSync);
    check(processor.value("mod1_jitter") == 40, "Random controls not attached");
    type->setSelectedId(1, juce::sendNotificationSync);
    check(!jitter->isVisible(), "LFO displayed random controls");
    type->setSelectedId(2, juce::sendNotificationSync);
    if (previews)
    {
        const auto savePreview = [&](const char* name)
        {
            juce::FileOutputStream stream(juce::File::getCurrentWorkingDirectory().getChildFile("build").getChildFile(name));
            stream.setPosition(0); stream.truncate();
            juce::PNGImageFormat png;
            png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), stream);
        };
        savePreview("effects-preview.png");
        auto* grains = dynamic_cast<juce::TextButton*>(findComponent(*editor, "grainsPage"));
        grains->onClick();
        savePreview("random-preview.png");
    }
    // Exercise Random through the actual processor, including sync pause and connection polarity.
    struct RandomPlayHead final : juce::AudioPlayHead
    {
        juce::AudioPlayHead::PositionInfo position;
        juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return position; }
    } playHead;
    playHead.position.setBpm(120); playHead.position.setIsPlaying(false);
    processor.setPlayHead(&playHead);
    processor.setValue("mod1_sync", 3); processor.setValue("mod1_polarity", 1);
    processor.setValue("mod1_connections", 1); processor.setValue("mod1_link1_target", 18);
    processor.setValue("mod1_link1_depth", 25); processor.setValue("pitch", 0);
    processor.prepareToPlay(48000, 256);
    const auto process = [&]
    {
        juce::AudioBuffer<float> audio(2, 256); audio.clear(); juce::MidiBuffer midi;
        processor.processBlock(audio, midi);
    };
    process();
    const float stopped = processor.modulationOutput(0);
    for (int i = 0; i < 10; ++i) process();
    check(processor.modulationOutput(0) == stopped, "Synced Random advanced while stopped");
    playHead.position.setIsPlaying(true);
    for (int i = 0; i < 20; ++i) process();
    const float randomOutput = processor.modulationOutput(0);
    check(randomOutput != stopped && std::abs(processor.liveValue(0, 18) - 12 * randomOutput) < 0.06f,
          "Random did not drive signed modulation contributions");
    processor.setPlayHead(nullptr); processor.releaseResources();
    std::cout << "Random, ordered effect lanes, picker, scrolling, EQ response, bypass and state recall passed\n";
}

void checkGrainFractions(bool preview)
{
    auto render = [](int assignedGroup, bool distortion, bool reverb, bool eq)
    {
        GranularProcessor processor;
        processor.setValue("fractions", 4);
        processor.setValue("mix", 100); processor.setValue("density", 30);
        for (int group = 0; group < 4; ++group)
            processor.setValue("chance" + juce::String(group), group == assignedGroup ? 100.0f : 0.0f);
        processor.setValue("fraction1_drive", 24); processor.setValue("fraction1_distMix", distortion ? 100.0f : 0.0f);
        processor.setValue("fraction1_reverb", reverb ? 100.0f : 0.0f); processor.setValue("fraction1_room", 80);
        processor.setValue("fraction1_low", eq ? 12.0f : 0.0f);
        processor.setPlayConfigDetails(1, 1, 48000, 256); processor.prepareToPlay(48000, 256);
        std::vector<float> result(48000 * 3);
        juce::MidiBuffer midi;
        for (int offset = 0; offset < static_cast<int>(result.size()); offset += 256)
        {
            const int count = juce::jmin(256, static_cast<int>(result.size()) - offset);
            juce::AudioBuffer<float> audio(1, count);
            for (int i = 0; i < count; ++i)
                audio.setSample(0, i, offset + i < 19200 ? 0.05f * std::sin(juce::MathConstants<float>::twoPi * 100 * (offset + i) / 48000) : 0);
            if (offset > 57600) processor.setValue("chance0", 0);
            processor.processBlock(audio, midi);
            for (int i = 0; i < count; ++i)
            {
                const float sample = audio.getSample(0, i);
                check(std::isfinite(sample) && std::abs(sample) < 4, "Unstable grain fraction effects");
                result[offset + i] = sample;
            }
        }
        processor.releaseResources();
        return result;
    };
    const auto plain = render(1, false, false, false);
    const auto unusedEffects = render(1, true, true, true);
    const auto distorted = render(0, true, false, false);
    const auto equalized = render(0, false, false, true);
    const auto reverberant = render(0, false, true, false);
    double difference = 0, plainEnergy = 0, eqEnergy = 0, tail = 0;
    for (size_t i = 0; i < plain.size(); ++i)
    {
        check(std::abs(plain[i] - unusedEffects[i]) < 0.000001f, "Fraction FX leaked into grains assigned to another group");
        difference += std::abs(plain[i] - distorted[i]);
        plainEnergy += plain[i] * plain[i]; eqEnergy += equalized[i] * equalized[i];
        if (i > 72000) tail += reverberant[i] * reverberant[i];
    }
    check(difference > 1 && eqEnergy > plainEnergy * 3, "Fraction distortion or EQ did not process assigned grains");
    check(tail > 0.000001, "Fraction reverb tail stopped after its percentage was set to zero");
    GranularProcessor processor;
    for (int group = 0; group < 4; ++group)
        check(processor.value("pitch" + juce::String(group)) == 0, "New fractions unexpectedly pitch-shifted grains by default");
    processor.setValue("fractions", 3); processor.setValue("m12_fractions", 3);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorIfNeeded());
    auto* fraction = dynamic_cast<juce::TextButton*>(findComponent(*editor, "fraction2"));
    check(fraction != nullptr, "Fraction effect selector missing"); fraction->onClick();
    auto* reverb = dynamic_cast<juce::Slider*>(findComponent(*editor, "fractionFx2"));
    check(reverb && reverb->isVisible(), "Fraction effect editor did not open");
    reverb->setValue(65, juce::sendNotificationSync);
    check(processor.value("fraction3_reverb") == 65 && processor.value("fraction1_reverb") == 0,
          "Fraction editor changed the wrong fraction");
    auto* moduleTwelve = dynamic_cast<juce::TextButton*>(findComponent(*editor, "module11")); moduleTwelve->onClick();
    reverb->setValue(40, juce::sendNotificationSync);
    check(processor.value("m12_fraction3_reverb") == 40 && processor.value("fraction3_reverb") == 65,
          "Fraction settings leaked between modules");
    auto* fxInput = findComponent(*editor, "modInput40"); // Fraction 3 reverb.
    check(fxInput && fxInput->isVisible(), "Fraction effect modulation socket missing");
    juce::MemoryBlock state; processor.getStateInformation(state);
    processor.setValue("m12_fraction3_reverb", 0);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(processor.value("m12_fraction3_reverb") == 40, "Fraction effect settings did not recall");
    auto legacy = juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize())));
    for (int i = legacy.getNumChildren(); --i >= 0;)
        if (legacy.getChild(i).getProperty("id").toString().contains("fraction")) legacy.removeChild(i, nullptr);
    juce::MemoryBlock oldState; juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), oldState);
    processor.setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
    check(processor.value("m12_fraction3_reverb") == 0 && processor.value("fraction3_reverb") == 0,
          "Older pitch-only patches inherited fraction effects");
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (preview)
    {
        auto* list = dynamic_cast<juce::Viewport*>(findComponent(*editor, "fractionList"));
        if (list != nullptr) list->setViewPosition(0, 88);
        juce::FileOutputStream stream(juce::File::getCurrentWorkingDirectory().getChildFile("dist/fractions-preview.png"));
        stream.setPosition(0); stream.truncate();
        juce::PNGImageFormat png; png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), stream);
    }
    std::cout << "Grain fraction allocation, effect isolation, tails, editing and state recall passed\n";
}

void checkFractionListAndAllWiring()
{
    GranularProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorIfNeeded());
    auto* add = dynamic_cast<juce::TextButton*>(findComponent(*editor, "addFraction"));
    check(add && processor.activeFractions(0) == 0, "Fraction list did not start empty");
    add->onClick(); add->onClick(); add->onClick();
    check(processor.activeFractions(0) == 3, "Add fraction did not append list rows");
    processor.setFractionShare(0, 0, 60); processor.setFractionShare(0, 1, 30);
    processor.setFractionShare(0, 2, 50);
    check(processor.value("chance0") == 60 && processor.value("chance1") == 30 && processor.value("chance2") == 10,
          "Share cap redistributed existing fractions or exceeded 100 percent");
    auto* share = dynamic_cast<juce::Slider*>(findComponent(*editor, "chance2"));
    check(share->snapValue(90, juce::Slider::notDragging) == 10, "User drag/text entry could exceed the available share");
    processor.setFractionShare(0, 0, 20);
    check(processor.value("chance1") == 30 && processor.value("chance2") == 10 && processor.availableFractionShare(0, 2) == 50,
          "Lowering one share changed another fraction");
    processor.setValue("fraction3_reverb", 45);
    processor.setValue("modCount", 1); processor.setValue("mod1_connections", 2);
    processor.setValue("mod1_link1_module", 0); processor.setValue("mod1_link1_target", GranularProcessor::fractionTarget(1, 1));
    processor.setValue("mod1_link2_module", 0); processor.setValue("mod1_link2_target", GranularProcessor::fractionTarget(2, 4));
    processor.removeFraction(0, 1);
    check(processor.activeFractions(0) == 2 && processor.value("chance0") == 20 && processor.value("chance1") == 10,
          "Remove fraction redistributed surviving shares");
    check(processor.value("fraction2_reverb") == 45 && processor.value("mod1_connections") == 1
          && processor.value("mod1_link1_target") == GranularProcessor::fractionTarget(1, 4), "Removal did not preserve settings/remap surviving cables");
    for (int module = 0; module < GranularProcessor::maxBands; ++module)
        while (processor.activeFractions(module) < GranularProcessor::fractionCount) processor.addFraction(module);
    for (int module = 0; module < GranularProcessor::maxBands; ++module)
        for (int group = 0; group < GranularProcessor::fractionCount; ++group)
            processor.setValue(GranularProcessor::moduleID(module, "chance" + juce::String(group)), 0);
    processor.addFraction(0);
    check(processor.activeFractions(0) == GranularProcessor::fractionCount, "Fraction limit was exceeded");
    processor.setValue("modCount", GranularProcessor::maxModulators);
    for (int source = 0; source < GranularProcessor::maxModulators; ++source)
    {
        processor.setValue(GranularProcessor::modID(source, "wave"), 3);
        processor.setValue(GranularProcessor::modID(source, "connections"), 0);
    }
    // Recreate the editor after setup to ensure all list rows and outputs bind correctly on reopen.
    processor.editorBeingDeleted(editor.get());
    editor.reset(); editor.reset(processor.createEditorIfNeeded());
    check(editor != nullptr, "Editor did not reopen");
    auto* list = dynamic_cast<juce::Viewport*>(findComponent(*editor, "fractionList"));
    check(list != nullptr, "Scrollable fraction list missing");
    auto point = [&](juce::Component* component) { return editor->getLocalArea(component, component->getLocalBounds()).getCentre().toFloat(); };
    auto eventAt = [&](juce::Component* source, juce::Point<float> position)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), source->getLocalPoint(editor.get(), position),
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, source, source, now, {}, now, 1, true);
    };
    processor.setPlayConfigDetails(2, 2, 48000, 64); processor.prepareToPlay(48000, 64);
    int tested = 0;
    for (int sourceIndex = 0; sourceIndex < GranularProcessor::maxModulators; ++sourceIndex)
        for (int target = 0; target < GranularProcessor::modTargetCount; ++target)
        {
            for (int source = 0; source < GranularProcessor::maxModulators; ++source)
                processor.setValue(GranularProcessor::modID(source, "connections"), 0);
            const int module = target % GranularProcessor::maxBands;
            auto* output = findComponent(*editor, "modOutput" + juce::String(sourceIndex));
            check(output && output->isVisible(), "Missing modulation output icon");
            output->mouseDown(eventAt(output, point(output)));
            auto* tab = findComponent(*editor, "module" + juce::String(module));
            output->mouseDrag(eventAt(output, point(tab)));
            const int group = GranularProcessor::targetFraction(target);
            const int control = GranularProcessor::targetFractionControl(target);
            if (group >= 0 && control >= 2)
            {
                list->setViewPosition(0, group * 44);
                auto* button = findComponent(*editor, "fraction" + juce::String(group));
                output->mouseDrag(eventAt(output, point(button)));
            }
            else
            {
                auto* page = findComponent(*editor, target >= 16 && target < 22 ? "playbackPage" : "grainsPage");
                output->mouseDrag(eventAt(output, point(page)));
                if (group >= 0) list->setViewPosition(0, group * 44);
            }
            auto* input = findComponent(*editor, "modInput" + juce::String(target));
            check(input && input->isVisible(), "Missing visible modulation input: " + juce::String(target).toStdString());
            output->mouseDrag(eventAt(output, point(input))); output->mouseUp(eventAt(output, point(input)));
            check(processor.value(GranularProcessor::modID(sourceIndex, "connections")) == 1
                  && processor.value(GranularProcessor::connectionID(sourceIndex, 0, "module")) == module
                  && processor.value(GranularProcessor::connectionID(sourceIndex, 0, "target")) == target,
                  "Patch drag routed to the wrong control: " + juce::String(target).toStdString());
            processor.setValue(GranularProcessor::connectionID(sourceIndex, 0, "depth"), 10);
            juce::AudioBuffer<float> audio(2, 64); audio.clear(); juce::MidiBuffer midi;
            processor.processBlock(audio, midi);
            auto* parameter = processor.parameters.getParameter(GranularProcessor::moduleID(module, GranularProcessor::modTargets[target]));
            const auto range = parameter->getNormalisableRange();
            const float base = processor.value(GranularProcessor::moduleID(module, GranularProcessor::modTargets[target]));
            const float expected = range.snapToLegalValue(juce::jlimit(range.start, range.end,
                base + processor.modulationOutput(sourceIndex) * processor.value(GranularProcessor::connectionID(sourceIndex, 0, "depth")) * 0.01f * (range.end - range.start)));
            check(std::abs(processor.liveValue(module, target) - expected) < 0.002f,
                  "Wired control did not receive modulation: " + juce::String(target).toStdString() + " actual=" + juce::String(processor.liveValue(module, target)).toStdString() + " expected=" + juce::String(expected).toStdString() + " count=" + juce::String(processor.activeFractions(module)).toStdString());
            check(processor.value(GranularProcessor::moduleID(module, GranularProcessor::modTargets[target])) == base,
                  "Modulation overwrote the destination's base setting");
            ++tested;
        }
    processor.releaseResources();
    for (int module = 0; module < GranularProcessor::maxBands; ++module)
        check(findComponent(*editor, "moduleInput" + juce::String(module)) != nullptr, "Module input icon missing");
    for (int source = 0; source < GranularProcessor::maxModulators; ++source)
        processor.setValue(GranularProcessor::modID(source, "connections"), 0);
    auto* sourceSocket = findComponent(*editor, "modOutput0");
    auto* grainsPage = dynamic_cast<juce::TextButton*>(findComponent(*editor, "grainsPage"));
    grainsPage->onClick();
    for (int module = 0; module < GranularProcessor::maxBands; ++module)
    {
        processor.setValue("mod1_connections", 1); processor.setValue("mod1_link1_module", static_cast<float>(module));
        processor.setValue("mod1_link1_target", 0);
        auto* proxy = findComponent(*editor, "moduleInput" + juce::String(module));
        const auto before = editor->createComponentSnapshot(editor->getLocalBounds());
        proxy->mouseEnter(eventAt(proxy, point(proxy)));
        const auto hovered = editor->createComponentSnapshot(editor->getLocalBounds());
        const auto start = point(sourceSocket).roundToInt();
        int changed = 0;
        for (int y = start.y - 40; y < start.y - 8; ++y)
            for (int x = start.x - 40; x < start.x + 40; ++x)
                if (before.getPixelAt(x, y) != hovered.getPixelAt(x, y)) ++changed;
        check(changed > 10, "Module input hover did not reveal incoming cables");
        proxy->mouseExit(eventAt(proxy, point(proxy)));
        proxy->mouseDown(eventAt(proxy, point(proxy)));
        check(dynamic_cast<juce::TextButton*>(findComponent(*editor, "module" + juce::String(module)))->getToggleState(),
              "Module socket did not open its module");
        proxy->mouseExit(eventAt(proxy, point(proxy)));
    }
    processor.setValue("mod1_connections", 0);
    auto* effects = dynamic_cast<juce::TextButton*>(findComponent(*editor, "effectsPage"));
    auto* hiddenRow = findComponent(*editor, "fraction0"); list->setViewPosition(0, 0);
    const auto hiddenPoint = point(hiddenRow);
    effects->onClick();
    sourceSocket->mouseDown(eventAt(sourceSocket, point(sourceSocket)));
    sourceSocket->mouseDrag(eventAt(sourceSocket, hiddenPoint));
    sourceSocket->mouseUp(eventAt(sourceSocket, hiddenPoint));
    check(effects->getToggleState() && processor.value("mod1_connections") == 0,
          "Hidden fraction rows intercepted a cable drag");
    grainsPage->onClick();
    sourceSocket->mouseDown(eventAt(sourceSocket, point(sourceSocket)));
    sourceSocket->mouseDrag(eventAt(sourceSocket, {1090, 10})); sourceSocket->mouseUp(eventAt(sourceSocket, {1090, 10}));
    check(processor.value("mod1_connections") == 0, "Dropping a cable on empty space created a connection");
    std::cout << "Audited " << tested << " output-to-input routes, all module sockets, scrolled fraction rows and DSP destinations\n";
}

int runChecks(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    if (argc < 2) return 1;
    checkRandomAndEffects(argc > 2);
    checkGrainFractions(argc > 2);
    checkFractionListAndAllWiring();
    checkModulationContributions();
    checkAutomatedBandLayout();
    checkCrossovers();
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, argv[1]);
    if (descriptions.isEmpty()) { std::cerr << "VST3 scan failed\n"; return 1; }
    juce::String error;
    auto vstPlugin = format.createInstanceFromDescription(*descriptions[0], 48000, 256, error);
    if (!vstPlugin) { std::cerr << error << '\n'; return 1; }
    check(vstPlugin->getName() == "Banded Grains", "VST3 product was not renamed");
    // The native VST editor is an HWND, not a JUCE child tree. Exercise gestures
    // on the same editor source directly, and separately process the compiled VST3.
    std::unique_ptr<juce::AudioProcessor> plugin(createPluginFilter());
    // VST3 wrappers may expose a host bypass parameter in addition to our controls.
    juce::AudioProcessorParameter* mix = nullptr;
    int controlsFound = 0;
    for (auto* parameter : plugin->getParameters())
    {
        const auto name = parameter->getName(100);
        if (name == "Grain size" || name == "Density" || name == "Dry / Wet") ++controlsFound;
        if (name == "Dry / Wet") mix = parameter;
    }
    if (controlsFound != 3 || mix == nullptr)
    { std::cerr << "Expected granular controls not found\n"; return 2; }
    mix->setValueNotifyingHost(0.75f);
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    mix->setValueNotifyingHost(0.0f);
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (std::abs(mix->getValue() - 0.75f) > 0.001f)
    { std::cerr << "State recall failed\n"; return 3; }
    std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
    {
        if (!editor || editor->getWidth() <= 0 || editor->getHeight() <= 0)
        { std::cerr << "Editor creation failed\n"; return 4; }
    }
    auto parameter = [&](const juce::String& name) -> juce::AudioProcessorParameter*
    {
        for (auto* p : plugin->getParameters()) if (p->getName(100) == name) return p;
        throw std::runtime_error("Missing parameter: " + name.toStdString());
    };
    auto* spectrum = findComponent(*editor, "spectrum");
    check(spectrum != nullptr, "Spectrum component missing");
    click(*spectrum, 1000, 2); click(*spectrum, 200, 2); click(*spectrum, 6000, 2);
    check(std::abs(parameter("Band count")->getValue() - 3.0f / 11) < 0.001f, "Double-click did not create four bands");
    click(*spectrum, 200, 1, true);
    check(std::abs(parameter("Band count")->getValue() - 2.0f / 11) < 0.001f, "Divider removal failed");
    click(*spectrum, 200, 2);
    click(*spectrum, 1000, 1); click(*spectrum, 1500, 1, false, true);
    check(std::abs(parameter("Split 2")->getValue() - parameter("Split 2")->getValueForText("1500")) < 0.001f,
          "Divider drag failed");
    auto* route = dynamic_cast<juce::ComboBox*>(findComponent(*editor, "route1"));
    check(route != nullptr, "Route selector missing");
    route->setSelectedId(3, juce::sendNotificationSync);
    auto* envelope = findComponent(*editor, "envelope");
    check(envelope != nullptr, "Draggable envelope missing");
    auto dragSlope = [&](float start, float end)
    {
        const auto time = juce::Time::getCurrentTime();
        auto event = [&](float x)
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {x, 55},
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                envelope, envelope, time, {start, 55}, time, 1, true);
        };
        envelope->mouseDown(event(start)); envelope->mouseDrag(event(end)); envelope->mouseUp(event(end));
    };
    dragSlope(120, 20);
    check(parameter("M3 Grain shape")->getValue() > 0.5f, "Attack slope drag did not change attack");
    check(parameter("M3 Grain release")->getValue() == 0, "Attack drag changed release");
    const auto attack = parameter("M3 Grain shape")->getValue();
    dragSlope(envelope->getWidth() - 120.0f, envelope->getWidth() - 20.0f);
    check(parameter("M3 Grain release")->getValue() > 0.5f, "Release slope drag did not change release");
    check(parameter("M3 Grain shape")->getValue() == attack, "Release drag changed attack");
    auto* step = dynamic_cast<juce::Slider*>(findComponent(*editor, "pitch0"));
    auto* chance = dynamic_cast<juce::Slider*>(findComponent(*editor, "chance0"));
    check(step != nullptr && chance != nullptr, "Pitch controls missing");
    step->setValue(12, juce::sendNotificationSync);
    chance->setValue(35, juce::sendNotificationSync);
    check(std::abs(parameter("M3 Chance 1")->getValue() - 0.35f) < 0.001f, "Pitch probability not attached to module");
    parameter("M3 Grain shape")->setValueNotifyingHost(1);
    plugin->getStateInformation(state);
    parameter("Band count")->setValueNotifyingHost(0);
    parameter("Band 2 route")->setValueNotifyingHost(0);
    parameter("M3 Grain shape")->setValueNotifyingHost(0);
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(std::abs(parameter("Band count")->getValue() - 3.0f / 11) < 0.001f, "Band count recall failed");
    check(std::abs(parameter("Band 2 route")->getValue() - 2.0f / 11) < 0.001f, "Routing recall failed");
    check(parameter("M3 Grain shape")->getValue() == 1, "Shape recall failed");
    check(parameter("M3 Grain release")->getValue() > 0.5f, "Release recall failed");
    check(std::abs(parameter("M3 Chance 1")->getValue() - 0.35f) < 0.001f, "Pitch chance recall failed");
    check(parameter("M1 Chance 1")->getValue() == 0, "Module pitch settings leaked");
    // Old sessions had only a symmetric shape parameter.
    auto legacy = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    auto legacyTree = juce::ValueTree::fromXml(*legacy);
    legacyTree.removeChild(legacyTree.getChildWithProperty("id", "m3_release"), nullptr);
    juce::MemoryBlock legacyState;
    juce::AudioProcessor::copyXmlToBinary(*legacyTree.createXml(), legacyState);
    plugin->setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
    check(parameter("M3 Grain release")->getValue() == 1, "Legacy symmetric envelope migration failed");
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    // Verify routing changes the sound in the intended frequency band.
    auto* direct = dynamic_cast<GranularProcessor*>(plugin.get());
    check(direct != nullptr, "Direct processor unavailable");
    direct->setValue("bands", 2); direct->setValue("split0", 1000);
    direct->setValue("route0", 0); direct->setValue("route1", 1);
    direct->setValue("mix", 0); direct->setValue("m2_mix", 100);
    direct->setValue("m2_density", 2); direct->setValue("m2_size", 20);
    auto measureTones = [&]()
    {
        plugin->setPlayConfigDetails(2, 2, 48000, 256); plugin->prepareToPlay(48000, 256);
        juce::AudioBuffer<float> audio(2, 256); juce::MidiBuffer midi;
        std::array<double, 2> real {}, imaginary {};
        for (int block = 0; block < 375; ++block)
        {
            for (int i = 0; i < 256; ++i)
            {
                const double t = (block * 256 + i) / 48000.0;
                const float input = static_cast<float>(0.1 * (std::sin(2 * juce::MathConstants<double>::pi * 100 * t)
                                                           + std::sin(2 * juce::MathConstants<double>::pi * 8000 * t)));
                audio.setSample(0, i, input); audio.setSample(1, i, input);
            }
            plugin->processBlock(audio, midi);
            if (block < 188) continue;
            for (int i = 0; i < 256; ++i)
                for (int tone = 0; tone < 2; ++tone)
                {
                    const double phase = 2 * juce::MathConstants<double>::pi * (tone == 0 ? 100 : 8000)
                                       * (block * 256 + i) / 48000.0;
                    real[tone] += audio.getSample(0, i) * std::cos(phase);
                    imaginary[tone] += audio.getSample(0, i) * std::sin(phase);
                }
        }
        plugin->releaseResources();
        return std::array<double, 2>{2 * std::hypot(real[0], imaginary[0]) / (187 * 256),
                                     2 * std::hypot(real[1], imaginary[1]) / (187 * 256)};
    };
    const auto separate = measureTones();
    direct->setValue("route1", 0);
    const auto combined = measureTones();
    check(separate[0] > 0.085 && separate[1] < 0.025, "Band routing did not isolate high-frequency granular processing");
    check(combined[0] > 0.085 && combined[1] > 0.085, "Routing bands back to the dry module lost audio");
    auto* listenLow = dynamic_cast<juce::TextButton*>(findComponent(*editor, "listen0"));
    auto* listenHigh = dynamic_cast<juce::TextButton*>(findComponent(*editor, "listen1"));
    check(listenLow != nullptr && listenHigh != nullptr, "Band listen buttons missing");
    listenLow->onClick();
    const auto lowOnly = measureTones();
    check(lowOnly[0] > 0.085 && lowOnly[1] < 0.002, "Low-band listen did not isolate low frequencies");
    listenHigh->onClick();
    check(direct->listenedBand() == 1, "Band listening is not exclusive");
    const auto highOnly = measureTones();
    check(highOnly[1] > 0.085 && highOnly[0] < 0.002, "High-band listen did not isolate high frequencies");
    listenHigh->onClick();
    check(direct->listenedBand() == -1, "Clicking listen again did not restore full output");
    const auto restoredOutput = measureTones();
    check(restoredOutput[0] > 0.085 && restoredOutput[1] > 0.085, "Listen toggle lost the full output");
    auto* soloModuleLow = dynamic_cast<juce::TextButton*>(findComponent(*editor, "moduleSolo0"));
    auto* soloModuleHigh = dynamic_cast<juce::TextButton*>(findComponent(*editor, "moduleSolo1"));
    check(soloModuleLow && soloModuleHigh, "Module solo buttons missing");
    direct->setValue("route1", 1); direct->setValue("m2_mix", 0);
    soloModuleLow->onClick();
    check(direct->soloedModule() == 0 && soloModuleLow->getToggleState(), "Module solo button did not activate");
    const auto moduleLow = measureTones();
    check(moduleLow[0] > 0.085 && moduleLow[1] < 0.002, "Low module solo did not isolate its output");
    soloModuleHigh->onClick();
    check(direct->soloedModule() == 1 && !soloModuleLow->getToggleState(), "Module solos are not exclusive");
    const auto moduleHigh = measureTones();
    check(moduleHigh[1] > 0.085 && moduleHigh[0] < 0.002, "High module solo did not isolate its output");
    direct->setValue("m2_mix", 100);
    const auto processedSolo = measureTones();
    check(processedSolo[0] < 0.002 && processedSolo[1] < 0.025, "Module solo bypassed granular processing");
    listenLow->onClick();
    check(direct->soloedModule() == -1 && direct->listenedBand() == 0, "Band audition did not replace module solo");
    soloModuleHigh->onClick();
    check(direct->listenedBand() == -1 && direct->soloedModule() == 1, "Module solo did not replace band audition");
    soloModuleHigh->onClick();
    check(direct->soloedModule() == -1 && !soloModuleHigh->getToggleState(), "Module solo did not toggle off");
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (int channels : {1, 2})
    {
        plugin->setPlayConfigDetails(channels, channels, 48000, 256);
        if (plugin->getTotalNumInputChannels() != channels)
        { std::cerr << "Channel layout failed\n"; return 5; }
        plugin->prepareToPlay(48000, 256);
        mix->setValueNotifyingHost(1.0f);
        double energy = 0.0;
        juce::AudioBuffer<float> audio(channels, 1024);
        juce::MidiBuffer midi;
        for (int block = 0; block < 500; ++block)
        {
            const int blockSize = std::array<int, 4>{1, 64, 257, 1024}[block % 4];
            audio.setSize(channels, blockSize, false, false, true);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    audio.setSample(ch, i, 0.1f * std::sin(static_cast<float>((block * 256 + i) * 0.05)));
            plugin->processBlock(audio, midi);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto value = audio.getSample(ch, i);
                    if (!std::isfinite(value) || std::abs(value) > 1.0f)
                    { std::cerr << "Unstable plugin audio\n"; return 6; }
                    if (block > 200) energy += value * value;
                }
            if (block % 10 == 0) pumpMessages(2);
        }
        if (energy < 1.0) { std::cerr << "Wet plugin audio is silent\n"; return 7; }
        plugin->releaseResources();
    }
    direct->setValue("bands", 1);
    for (int i = 0; i < GranularProcessor::maxSplits; ++i)
        direct->addSplit(60.0f * std::pow(200.0f, static_cast<float>(i) / 10));
    check(direct->bandCount() == 12, "Could not create twelve bands");
    direct->addSplit(17000);
    check(direct->bandCount() == 12, "Exceeded twelve-band limit");
    const auto twelveSplits = direct->boundaries();
    check(std::is_sorted(twelveSplits.begin(), twelveSplits.end()), "Twelve crossover boundaries not sorted");
    for (int i = 0; i < 12; ++i)
    {
        direct->setValue("route" + juce::String(i), static_cast<float>(i));
        direct->setValue(GranularProcessor::moduleID(i, "mix"), 50);
    }
    direct->setValue("m12_release", 70);
    direct->setValue("m12_chance0", 30);
    direct->setValue("m12_pitch0", -12);
    plugin->getStateInformation(state);
    direct->setValue("bands", 1);
    direct->setValue("route11", 0);
    direct->setValue("m12_chance0", 0);
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(direct->bandCount() == 12 && direct->value("route11") == 11 && direct->value("m12_chance0") == 30,
          "Twelve-band/module state recall failed");
    for (int i = 0; i < 11; ++i) direct->setValue("split" + juce::String(i), 60.0f * std::pow(100.0f, i / 10.0f));
    for (int i = 0; i < 12; ++i) direct->setValue(GranularProcessor::moduleID(i, "mix"), 0);
    direct->setValue("m12_mix", 100); direct->setValue("m12_density", 2); direct->setValue("m12_size", 20);
    direct->setValue("m12_chance0", 0);
    const auto twelfthWet = measureTones();
    direct->setValue("route11", 0);
    const auto twelfthDry = measureTones();
    check(twelfthWet[0] > 0.085 && twelfthWet[1] < 0.04 && twelfthDry[1] > 0.085,
          "Twelfth module did not independently process the highest band");
    direct->toggleBandListen(11);
    const auto twelfthOnly = measureTones();
    check(twelfthOnly[0] < 0.002 && twelfthOnly[1] > 0.05, "Twelfth band audition did not isolate high frequencies");
    auto* lastSolo = dynamic_cast<juce::TextButton*>(findComponent(*editor, "moduleSolo11"));
    check(lastSolo != nullptr, "Twelfth module solo missing");
    lastSolo->onClick();
    check(direct->soloedModule() == 11 && direct->listenedBand() == -1, "Twelfth module cannot be soloed");
    direct->setValue("route11", 11); direct->setValue("m12_mix", 0);
    const auto lastSoloAudio = measureTones();
    check(lastSoloAudio[0] < 0.002 && lastSoloAudio[1] > 0.05, "Twelfth module solo did not isolate its audio");
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(direct->soloedModule() == -1, "Preset recall left module solo active");
    auto* lastModule = dynamic_cast<juce::TextButton*>(findComponent(*editor, "module11"));
    check(lastModule != nullptr, "Twelfth module selector missing");
    lastModule->onClick();
    auto* lastSize = dynamic_cast<juce::Slider*>(findComponent(*editor, "size"));
    lastSize->setValue(180, juce::sendNotificationSync);
    check(direct->value("m12_size") == 180, "Twelfth module editor did not bind to controls");
    pumpMessages(100);
    auto* lastListen = dynamic_cast<juce::TextButton*>(findComponent(*editor, "listen11"));
    check(lastListen != nullptr && lastListen->isVisible(), "Twelfth band listen missing");
    lastListen->onClick();
    check(direct->listenedBand() == 11, "Twelfth band cannot be auditioned");
    direct->removeSplit(10);
    check(direct->bandCount() == 11 && direct->listenedBand() == -1, "Removing a split left stale listening active");
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    pumpMessages(100);
    lastModule->onClick();
    auto* feedbackSlider = dynamic_cast<juce::Slider*>(findComponent(*editor, "feedback"));
    auto* repeatSlider = dynamic_cast<juce::Slider*>(findComponent(*editor, "repeat"));
    auto* dampingSlider = dynamic_cast<juce::Slider*>(findComponent(*editor, "damping"));
    check(feedbackSlider && repeatSlider && dampingSlider, "Feedback controls missing");
    feedbackSlider->setValue(40, juce::sendNotificationSync);
    repeatSlider->setValue(450, juce::sendNotificationSync);
    dampingSlider->setValue(3500, juce::sendNotificationSync);
    check(direct->value("m12_feedback") == 40 && direct->value("m12_repeat") == 450 && direct->value("m12_damping") == 3500,
          "Module feedback controls not attached");
    auto* addMod = dynamic_cast<juce::TextButton*>(findComponent(*editor, "addModulator"));
    check(addMod != nullptr, "Add modulator button missing");
    addMod->onClick();
    check(direct->value("modCount") == 1, "Could not add shared modulator");
    auto* output = findComponent(*editor, "modOutput0");
    auto cableEvent = [&](juce::Point<float> point)
    {
        const auto now = juce::Time::getCurrentTime();
        const auto local = output->getLocalPoint(editor.get(), point);
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            output, output, now, {}, now, 1, true);
    };
    auto patch = [&](int target, int switchModule)
    {
        output->mouseDown(cableEvent(output->getBounds().getCentre().toFloat()));
        if (switchModule >= 0)
        {
            auto* tab = findComponent(*editor, "module" + juce::String(switchModule));
            output->mouseDrag(cableEvent(tab->getBounds().getCentre().toFloat()));
        }
        auto* input = findComponent(*editor, "modInput" + juce::String(target));
        const auto end = editor->getLocalArea(input, input->getLocalBounds()).getCentre().toFloat();
        output->mouseDrag(cableEvent(end)); output->mouseUp(cableEvent(end));
    };
    patch(5, -1); // Module 12 feedback.
    patch(2, 0);  // Switch module while dragging; same source drives Module 1 mix.
    check(direct->value("mod1_connections") == 2 && direct->value("mod1_link1_module") == 11
          && direct->value("mod1_link2_module") == 0, "Cross-module patch cable routing failed");
    check(direct->value("mod1_link1_depth") == 0,
          "New connection did not default to a zero-depth contribution");
    auto* polarity = dynamic_cast<juce::ComboBox*>(findComponent(*editor, "modPolarity"));
    auto* depth = dynamic_cast<juce::Slider*>(findComponent(*editor, "connectionDepth0"));
    check(polarity && depth && depth->isVisible(), "Contribution controls missing");
    polarity->setSelectedId(2, juce::sendNotificationSync);
    depth->setValue(-25, juce::sendNotificationSync);
    check(direct->value("mod1_polarity") == 1 && direct->value("mod1_link1_depth") == -25,
          "Polarity or signed depth control not attached");
    polarity->setSelectedId(1, juce::sendNotificationSync);
    direct->setValue("mod1_link1_depth", 20);
    direct->setValue("mod1_link2_depth", -10);
    const float baseFeedback = direct->value("m12_feedback"), baseMix = direct->value("mix");
    direct->setPlayConfigDetails(2, 2, 48000, 256); direct->prepareToPlay(48000, 256);
    for (int wave = 0; wave < 6; ++wave)
    {
        direct->setValue("mod1_wave", static_cast<float>(wave));
        for (int block = 0; block < 100; ++block)
        {
            juce::AudioBuffer<float> audio(2, 256); audio.clear(); juce::MidiBuffer midi;
            direct->processBlock(audio, midi);
            const float value = direct->modulationOutput(0);
            check(std::abs(direct->liveValue(11, 5) - (baseFeedback + 19 * value)) < 0.06f,
                  "Feedback connection range incorrect");
            check(std::abs(direct->liveValue(0, 2) - (baseMix - 10 * value)) < 0.06f,
                  "Same source did not drive a separate module range");
        }
    }
    check(direct->value("m12_feedback") == baseFeedback && direct->value("mix") == baseMix,
          "Modulation overwrote saved base parameters");
    direct->releaseResources();
    plugin->getStateInformation(state);
    direct->setValue("modCount", 0); direct->setValue("m12_feedback", 0);
    direct->setValue("mod1_link1_depth", 10);
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(direct->value("modCount") == 1 && direct->value("mod1_connections") == 2
          && direct->value("mod1_link1_depth") == 20 && direct->value("m12_feedback") == baseFeedback,
          "Feedback or shared modulation patch recall failed");
    check(direct->getTailLengthSeconds() > 2, "Feedback tail not reported to host");
    auto oldPreset = juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize())));
    for (int i = oldPreset.getNumChildren(); --i >= 0;)
    {
        const auto id = oldPreset.getChild(i).getProperty("id").toString();
        if (id.startsWith("mod") || id == "feedback" || id == "repeat" || id == "damping"
            || id.endsWith("_feedback") || id.endsWith("_repeat") || id.endsWith("_damping"))
            oldPreset.removeChild(i, nullptr);
    }
    juce::MemoryBlock oldPresetBytes;
    juce::AudioProcessor::copyXmlToBinary(*oldPreset.createXml(), oldPresetBytes);
    plugin->setStateInformation(oldPresetBytes.getData(), static_cast<int>(oldPresetBytes.getSize()));
    check(direct->value("modCount") == 0 && direct->value("m12_feedback") == 0
          && direct->value("m12_repeat") == 350 && direct->value("m12_damping") == 6000,
          "Older presets did not disable feedback and modulation");
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    struct TestPlayHead final : juce::AudioPlayHead
    {
        juce::AudioPlayHead::PositionInfo position;
        juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return position; }
    } playHead;
    playHead.position.setBpm(60); playHead.position.setIsPlaying(false);
    direct->setPlayHead(&playHead);
    direct->setValue("mod1_wave", 2); direct->setValue("mod1_sync", 3);
    direct->prepareToPlay(48000, 256);
    auto processSilent = [&]
    {
        juce::AudioBuffer<float> audio(2, 256); audio.clear(); juce::MidiBuffer midi;
        direct->processBlock(audio, midi);
    };
    processSilent();
    check(direct->modulationOutput(0) == 0, "Beat-synced source advanced during stopped transport");
    playHead.position.setIsPlaying(true);
    processSilent();
    check(std::abs(direct->modulationOutput(0) - 256.0f / 48000) < 0.00001f, "Beat-sync rate incorrect at 60 BPM");
    playHead.position.setBpm(120); processSilent();
    check(std::abs(direct->modulationOutput(0) - 768.0f / 48000) < 0.00001f, "Beat-sync ignored host tempo change");
    direct->setPlayHead(nullptr); direct->releaseResources();
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    pumpMessages(100);
    // Cables are hidden until an output is hovered; off-module cables end at the module socket.
    output->mouseExit(cableEvent(output->getBounds().getCentre().toFloat()));
    const auto hiddenWires = editor->createComponentSnapshot(editor->getLocalBounds());
    output->mouseEnter(cableEvent(output->getBounds().getCentre().toFloat()));
    const auto visibleWires = editor->createComponentSnapshot(editor->getLocalBounds());
    auto* moduleTwelve = findComponent(*editor, "module11");
    int wirePixels = 0;
    for (int y = moduleTwelve->getY(); y < moduleTwelve->getBottom(); ++y)
        for (int x = moduleTwelve->getX(); x < moduleTwelve->getRight(); ++x)
            if (hiddenWires.getPixelAt(x, y) != visibleWires.getPixelAt(x, y)) ++wirePixels;
    check(wirePixels > 30, "Hover did not reveal off-module cable destination");
    output->mouseExit(cableEvent(output->getBounds().getCentre().toFloat()));
    const auto wiresGone = editor->createComponentSnapshot(editor->getLocalBounds());
    check(wiresGone.getPixelAt(moduleTwelve->getRight() - 2, moduleTwelve->getY() + 2)
          == hiddenWires.getPixelAt(moduleTwelve->getRight() - 2, moduleTwelve->getY() + 2), "Wires stayed visible after hover ended");
    addMod->onClick();
    output = findComponent(*editor, "modOutput1");
    patch(2, -1);
    check(direct->value("mod2_link1_module") == 0 && direct->value("mod2_connections") == 1,
          "Second source could not connect to an existing input");
    auto* mixInput = findComponent(*editor, "modInput2");
    const auto noIncoming = editor->createComponentSnapshot(editor->getLocalBounds());
    mixInput->mouseEnter(cableEvent({}));
    const auto incoming = editor->createComponentSnapshot(editor->getLocalBounds());
    for (int source = 0; source < 2; ++source)
    {
        auto* socket = findComponent(*editor, "modOutput" + juce::String(source));
        const auto centre = socket->getBounds().getCentre();
        int changed = 0;
        for (int y = centre.y - 30; y < centre.y - 12; ++y)
            for (int x = centre.x - 8; x <= centre.x + 8; ++x)
                if (noIncoming.getPixelAt(x, y) != incoming.getPixelAt(x, y)) ++changed;
        check(changed > 5, "Input hover did not reveal every incoming source");
    }
    mixInput->mouseExit(cableEvent({}));
    auto* playbackPage = dynamic_cast<juce::TextButton*>(findComponent(*editor, "playbackPage"));
    auto* grainsPage = dynamic_cast<juce::TextButton*>(findComponent(*editor, "grainsPage"));
    check(playbackPage && grainsPage, "Playback panel selector missing");
    playbackPage->onClick();
    const float connectionsBeforeHiddenDrop = direct->value("mod2_connections");
    patch(0, -1); // Grain size socket is hidden on the Playback page.
    check(direct->value("mod2_connections") == connectionsBeforeHiddenDrop,
          "A cable connected to a hidden grain control");
    patch(16, -1); // Position socket is visible on the Playback page.
    check(direct->value("mod2_connections") == connectionsBeforeHiddenDrop + 1
          && direct->value("mod2_link2_target") == 16,
          "A cable could not connect to a visible playback control");
    direct->setValue("mod2_connections", connectionsBeforeHiddenDrop);
    pumpMessages(50);
    const std::array<float, 5> playbackValues {300, 350, -7, 45, 70};
    for (int i = 0; i < 5; ++i)
    {
        auto* slider = dynamic_cast<juce::Slider*>(findComponent(*editor, GranularProcessor::modTargets[16 + i]));
        check(slider && slider->isVisible(), "Playback control missing");
        slider->setValue(playbackValues[i], juce::sendNotificationSync);
        check(std::abs(direct->value(GranularProcessor::modTargets[16 + i]) - playbackValues[i]) < 0.01f, "Playback control not attached");
    }
    auto* freeze = dynamic_cast<juce::TextButton*>(findComponent(*editor, "freeze"));
    freeze->triggerClick(); pumpMessages(50);
    check(direct->value("freeze") == 1, "Freeze button not attached");
    plugin->getStateInformation(state);
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(direct->value("freeze") == 0 && direct->value("pitch") == -7, "Playback recall or freeze reset failed");
    direct->prepareToPlay(48000, 256); processSilent();
    GranularProcessor::GrainVisuals grainVisuals;
    check(direct->readGrainVisuals(grainVisuals), "No live grain visual data");
    const auto beforeHead = grainVisuals[0].recordHead;
    processSilent();
    check(direct->readGrainVisuals(grainVisuals) && grainVisuals[0].recordHead != beforeHead, "Grain display recording head did not advance");
    bool grainVisible = false;
    for (const auto& grain : grainVisuals[0].grains) if (grain.speed != 0) grainVisible = true;
    check(grainVisible && findComponent(*editor, "grainVisualizer"), "Playback visualization has no active grains");
    processSilent(); pumpMessages(50);
    if (argc > 2)
    {
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        juce::FileOutputStream stream(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]).getSiblingFile("playback-preview.png"));
        juce::PNGImageFormat png; png.writeImageToStream(image, stream);
    }
    grainsPage->onClick();
    direct->releaseResources();
    if (argc > 2)
    {
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));
        check(output.openedOk(), "Could not write editor preview");
        output.setPosition(0); output.truncate();
        juce::PNGImageFormat png; png.writeImageToStream(image, output);
    }
    plugin->getStateInformation(state);
    vstPlugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (int channels : {1, 2})
    {
        vstPlugin->setPlayConfigDetails(channels, channels, 48000, 257);
        vstPlugin->prepareToPlay(48000, 257);
        juce::AudioBuffer<float> audio(channels, 257);
        juce::MidiBuffer midi;
        double energy = 0;
        for (int block = 0; block < 600; ++block)
        {
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < 257; ++i) audio.setSample(ch, i, 0.1f * std::sin((block * 257 + i) * 0.05f));
            vstPlugin->processBlock(audio, midi);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < 257; ++i)
                {
                    const float sample = audio.getSample(ch, i);
                    check(std::isfinite(sample) && std::abs(sample) < 1, "Invalid VST3 output");
                    if (block > 200) energy += sample * sample;
                }
        }
        check(energy > 1, "VST3 multiband output is silent");
        vstPlugin->releaseResources();
    }
    std::cout << "VST3 load, editor, split interactions, frequency-specific routing, routing/shape recall, mono/stereo and variable blocks passed\n";
    return 0;
}

int main(int argc, char** argv)
{
    try { return runChecks(argc, argv); }
    catch (const std::exception& error) { std::cerr << "Check failed: " << error.what() << std::endl; return 10; }
}

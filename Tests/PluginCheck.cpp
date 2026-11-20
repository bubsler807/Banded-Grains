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

int runChecks(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    if (argc < 2) return 1;
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
        const auto end = input->getBounds().getCentre().toFloat();
        output->mouseDrag(cableEvent(end)); output->mouseUp(cableEvent(end));
    };
    patch(5, -1); // Module 12 feedback.
    patch(2, 0);  // Switch module while dragging; same source drives Module 1 mix.
    check(direct->value("mod1_connections") == 2 && direct->value("mod1_link1_module") == 11
          && direct->value("mod1_link2_module") == 0, "Cross-module patch cable routing failed");
    direct->setValue("mod1_link1_min", 20); direct->setValue("mod1_link1_max", 70);
    direct->setValue("mod1_link2_min", 10); direct->setValue("mod1_link2_max", 30);
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
            check(std::abs(direct->liveValue(11, 5) - (19 + 47.5f * value)) < 0.06f,
                  "Feedback connection range incorrect");
            check(std::abs(direct->liveValue(0, 2) - (10 + 20 * value)) < 0.06f,
                  "Same source did not drive a separate module range");
        }
    }
    check(direct->value("m12_feedback") == baseFeedback && direct->value("mix") == baseMix,
          "Modulation overwrote saved base parameters");
    direct->releaseResources();
    plugin->getStateInformation(state);
    direct->setValue("modCount", 0); direct->setValue("m12_feedback", 0);
    direct->setValue("mod1_link1_max", 10);
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(direct->value("modCount") == 1 && direct->value("mod1_connections") == 2
          && direct->value("mod1_link1_max") == 70 && direct->value("m12_feedback") == baseFeedback,
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

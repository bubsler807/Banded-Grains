#include "PluginProcessor.h"
#include "PluginEditor.h"

GranularProcessor::GranularProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameters())
{
    for (int m = 0; m < maxBands; ++m)
    {
        controls[m] = { parameters.getRawParameterValue(moduleID(m, "size")),
                        parameters.getRawParameterValue(moduleID(m, "density")),
                        parameters.getRawParameterValue(moduleID(m, "mix")),
                        parameters.getRawParameterValue(moduleID(m, "shape")),
                        parameters.getRawParameterValue(moduleID(m, "release")) };
        routeControls[m] = parameters.getRawParameterValue("route" + juce::String(m));
        for (int i = 0; i < 6; ++i) playbackControls[m][i] = parameters.getRawParameterValue(moduleID(m, modTargets[16 + i]));
        feedbackControls[m] = {parameters.getRawParameterValue(moduleID(m, "feedback")),
            parameters.getRawParameterValue(moduleID(m, "repeat")), parameters.getRawParameterValue(moduleID(m, "damping"))};
        modCount = parameters.getRawParameterValue("modCount");
        for (int target = 0; target < modTargetCount; ++target)
            modDestinations[m][target] = parameters.getParameter(moduleID(m, modTargets[target]));
        for (int slot = 0; slot < maxModulators; ++slot)
        {
            auto pointer = [&](const char* id) { return parameters.getRawParameterValue(modID(slot, id)); };
            modControls[slot] = {pointer("wave"), pointer("rate"), pointer("sync"), pointer("connections")};
            for (int link = 0; link < maxConnections; ++link)
            {
                auto connection = [&](const char* id) { return parameters.getRawParameterValue(connectionID(slot, link, id)); };
                modConnections[slot][link] = {connection("module"), connection("target"), connection("min"), connection("max")};
            }
        }
        for (int i = 0; i < 4; ++i)
        {
            pitchControls[m][i] = parameters.getRawParameterValue(moduleID(m, "pitch" + juce::String(i)));
            chanceControls[m][i] = parameters.getRawParameterValue(moduleID(m, "chance" + juce::String(i)));
        }
    }
    for (int i = 0; i < maxSplits; ++i) splitControls[i] = parameters.getRawParameterValue("split" + juce::String(i));
    countControl = parameters.getRawParameterValue("bands");
}
void GranularProcessor::prepareToPlay(double rate, int)
{
    displayRate.store(static_cast<float>(rate));
    splitter.prepare(rate);
    for (int slot = 0; slot < maxModulators; ++slot)
        modStates[slot] = {0, 0.5f, static_cast<uint32_t>(1234567 + slot * 65537)};
    listenAudio.setSize(2, chunkSize);
    const int selected = listenedBand() < bandCount() ? listenedBand() : -1;
    mainGain.reset(rate, 0.01);
    mainGain.setCurrentAndTargetValue(selected < 0 ? 1.0f : 0.0f);
    for (int band = 0; band < maxBands; ++band)
    {
        listenGains[band].reset(rate, 0.01);
        listenGains[band].setCurrentAndTargetValue(band == selected ? 1.0f : 0.0f);
    }
    for (int m = 0; m < maxBands; ++m)
    {
        engines[m].prepare(rate);
        moduleSoloGains[m].reset(rate, 0.01);
        moduleSoloGains[m].setCurrentAndTargetValue(soloedModule() < 0 || soloedModule() == m ? 1.0f : 0.0f);
        moduleAudio[m].setSize(2, chunkSize);
        for (int band = 0; band < maxBands; ++band)
        {
            sends[band][m].reset(rate, 0.015);
            sends[band][m].setCurrentAndTargetValue(juce::roundToInt(routeControls[band]->load()) == m ? 1.0f : 0.0f);
        }
    }
}
bool GranularProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto output = layout.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && layout.getMainInputChannelSet() == output;
}
void GranularProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = getTotalNumInputChannels();
    const int bands = juce::jlimit(1, maxBands, juce::roundToInt(countControl->load()));
    double bpm = 120;
    bool advanceModulation = true;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            advanceModulation = position->getIsPlaying();
            if (const auto tempo = position->getBpm()) if (*tempo > 0) bpm = *tempo;
        }
    std::array<float, GranularProcessor::maxSplits> frequencies;
    for (int i = 0; i < maxSplits; ++i) frequencies[i] = splitControls[i]->load();
    splitter.configure(bands, frequencies);
    const int requested = listenedBand();
    const int selected = requested >= 0 && requested < bands ? requested : -1;
    const int solo = soloedModule();
    for (int m = 0; m < maxBands; ++m) moduleSoloGains[m].setTargetValue(solo < 0 || solo == m ? 1.0f : 0.0f);
    mainGain.setTargetValue(selected < 0 ? 1.0f : 0.0f);
    for (int band = 0; band < maxBands; ++band) listenGains[band].setTargetValue(band == selected ? 1.0f : 0.0f);
    for (int band = 0; band < maxBands; ++band)
        for (int m = 0; m < maxBands; ++m)
            sends[band][m].setTargetValue(juce::roundToInt(routeControls[band]->load()) == m ? 1.0f : 0.0f);
    // Fixed chunks avoid allocations for arbitrarily large host buffers.
    for (int offset = 0; offset < buffer.getNumSamples(); offset += chunkSize)
    {
        const int length = juce::jmin(chunkSize, buffer.getNumSamples() - offset);
        for (auto& audio : moduleAudio) audio.clear();
        listenAudio.clear();
        std::array<float, chunkSize> mainGains;
        for (int i = 0; i < length; ++i)
        {
            const float left = buffer.getSample(0, offset + i);
            const float right = buffer.getSample(channels - 1, offset + i);
            int start1, size1, start2, size2;
            spectrumFifo.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 != 0)
            {
                spectrumData[0][start1] = left; spectrumData[1][start1] = right;
                spectrumFifo.finishedWrite(1);
            }
            splitter.advance();
            mainGains[i] = mainGain.getNextValue();
            std::array<float, maxBands> audition;
            for (int band = 0; band < maxBands; ++band) audition[band] = listenGains[band].getNextValue();
            float weights[maxBands][maxBands];
            for (int band = 0; band < maxBands; ++band)
                for (int m = 0; m < maxBands; ++m) weights[band][m] = sends[band][m].getNextValue();
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto sections = splitter.processSample(ch, ch == 0 ? left : right);
                for (int band = 0; band < bands; ++band)
                    listenAudio.addSample(ch, i, sections[band] * audition[band]);
                for (int band = 0; band < bands; ++band)
                    for (int m = 0; m < maxBands; ++m)
                        moduleAudio[m].addSample(ch, i, sections[band] * weights[band][m]);
            }
        }
        std::array<float, maxModulators> modulationValues {};
        const int activeModulators = juce::jlimit(0, maxModulators, juce::roundToInt(modCount->load()));
        for (int slot = 0; slot < activeModulators; ++slot)
        {
            const auto& mod = modControls[slot];
            auto& state = modStates[slot];
            const int sync = juce::jlimit(0, 7, juce::roundToInt(mod.sync->load()));
            const double hz = sync == 0 ? mod.rate->load() : bpm / (60 * std::array<double, 7>{0.25, 0.5, 1, 2, 4, 8, 16}[sync - 1]);
            // Free LFOs run continuously. Beat-synced LFOs pause with transport.
            if (sync == 0 || advanceModulation) state.phase += hz * length / displayRate.load();
            if (state.phase >= 1)
            {
                state.phase -= std::floor(state.phase);
                state.random = state.random * 1664525u + 1013904223u;
                state.held = static_cast<float>(state.random >> 8) / 16777216.0f;
            }
            const float phase = static_cast<float>(state.phase);
            float wave = 0;
            switch (juce::roundToInt(mod.wave->load()))
            {
                case 1: wave = 1 - std::abs(2 * phase - 1); break;
                case 2: wave = phase; break;
                case 3: wave = 1 - phase; break;
                case 4: wave = phase < 0.5f ? 0 : 1; break;
                case 5: wave = state.held; break;
                default: wave = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phase); break;
            }
            modulationValues[slot] = wave;
            modOutputs[slot].store(wave);
        }

        for (int ch = 0; ch < channels; ++ch) buffer.clear(ch, offset, length);
        for (int m = 0; m < maxBands; ++m)
        {
            std::array<float, modTargetCount> values {controls[m].size->load(), controls[m].density->load(),
                controls[m].mix->load(), controls[m].shape->load(), controls[m].release->load(),
                feedbackControls[m][0]->load(), feedbackControls[m][1]->load(), feedbackControls[m][2]->load()};
            for (int i = 0; i < 4; ++i) { values[8 + i] = pitchControls[m][i]->load(); values[12 + i] = chanceControls[m][i]->load(); }
            for (int i = 0; i < 6; ++i) values[16 + i] = playbackControls[m][i]->load();
            for (int slot = 0; slot < activeModulators; ++slot)
            {
                const auto& mod = modControls[slot];
                const float wave = modulationValues[slot];
                const int links = juce::jlimit(0, maxConnections, juce::roundToInt(mod.connections->load()));
                for (int link = 0; link < links; ++link)
                {
                    const auto& connection = modConnections[slot][link];
                    if (juce::roundToInt(connection.module->load()) != m) continue;
                    const int target = juce::jlimit(0, modTargetCount - 1, juce::roundToInt(connection.target->load()));
                    const float minimum = connection.minimum->load() * 0.01f, maximum = connection.maximum->load() * 0.01f;
                    values[target] = modDestinations[m][target]->convertFrom0to1(juce::jlimit(0.0f, 1.0f, minimum + wave * (maximum - minimum)));
                    if (target >= 8 && target < 12) values[target] = std::round(values[target]);
                }
            }
            GranularEngine::PitchChoices pitches;
            for (int target = 0; target < modTargetCount; ++target) liveValues[m][target].store(values[target]);
            for (int i = 0; i < 4; ++i)
            {
                pitches.semitones[i] = values[8 + i];
                pitches.chances[i] = values[12 + i];
            }
            const GranularEngine::Playback playback {values[16], values[17], values[18], values[19] * 0.01f, values[20] * 0.01f, values[21] >= 0.5f};
            engines[m].process(moduleAudio[m].getArrayOfWritePointers(), channels, length,
                values[0], values[1], values[2] * 0.01f, values[3] * 0.01f,
                values[4] * 0.01f, &pitches, values[5] * 0.01f, values[6], values[7], &playback);
            // Keep every engine running so soloing preserves grain and feedback tails.
            for (int i = 0; i < length; ++i)
            {
                const float soloGain = moduleSoloGains[m].getNextValue();
                for (int ch = 0; ch < channels; ++ch)
                    buffer.addSample(ch, offset + i, moduleAudio[m].getSample(ch, i) * soloGain);
            }
        }
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                buffer.setSample(ch, offset + i, buffer.getSample(ch, offset + i) * mainGains[i] + listenAudio.getSample(ch, i));
    }
    int start1, size1, start2, size2;
    grainVisualFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 != 0)
    {
        for (int m = 0; m < maxBands; ++m) grainVisualData[start1][m] = engines[m].visualState();
        grainVisualFifo.finishedWrite(1);
    }
}
bool GranularProcessor::readGrainVisuals(GrainVisuals& result)
{
    bool read = false;
    for (int batch = 0; batch < 8; ++batch)
    {
        int start1, size1, start2, size2;
        grainVisualFifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 == 0) break;
        result = grainVisualData[start1];
        grainVisualFifo.finishedRead(1);
        read = true;
    }
    return read;
}
int GranularProcessor::readSpectrum(float* left, float* right, int maximum)
{
    int start1, size1, start2, size2;
    spectrumFifo.prepareToRead(maximum, start1, size1, start2, size2);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* output = ch == 0 ? left : right;
        std::copy_n(spectrumData[ch].data() + start1, size1, output);
        std::copy_n(spectrumData[ch].data() + start2, size2, output + size1);
    }
    spectrumFifo.finishedRead(size1 + size2);
    return size1 + size2;
}
std::array<float, GranularProcessor::maxSplits> GranularProcessor::boundaries() const
{
    std::array<float, GranularProcessor::maxSplits> result;
    for (int i = 0; i < maxSplits; ++i) result[i] = splitControls[i]->load();
    std::sort(result.begin(), result.begin() + bandCount() - 1);
    return result;
}
void GranularProcessor::setValue(const juce::String& id, float newValue)
{
    auto* parameter = parameters.getParameter(id);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(newValue));
    parameter->endChangeGesture();
}
void GranularProcessor::addSplit(float frequency)
{
    const int count = bandCount();
    if (count == maxBands) return;
    auto splits = boundaries();
    frequency = juce::jlimit(30.0f, displayMaxFrequency() * 0.95f, frequency);
    int index = 0;
    while (index < count - 1 && splits[index] < frequency) ++index;
    for (int i = 0; i < count - 1; ++i)
        if (std::abs(std::log(frequency / splits[i])) < 0.08f) return;
    for (int i = count - 1; i > index; --i) splits[i] = splits[i - 1];
    splits[index] = frequency;
    auditionSource.store(-1);
    for (int i = count; i > index + 1; --i)
        setValue("route" + juce::String(i), value("route" + juce::String(i - 1)));
    setValue("route" + juce::String(index + 1), static_cast<float>(count));
    for (int i = 0; i < count; ++i) setValue("split" + juce::String(i), splits[i]);
    setValue("bands", static_cast<float>(count + 1));
}
void GranularProcessor::removeSplit(int index)
{
    const int count = bandCount();
    if (index < 0 || index >= count - 1) return;
    auditionSource.store(-1);
    auto splits = boundaries();
    for (int i = index; i < count - 2; ++i) splits[i] = splits[i + 1];
    for (int i = 0; i < count - 2; ++i) setValue("split" + juce::String(i), splits[i]);
    for (int i = index + 1; i < count - 1; ++i)
        setValue("route" + juce::String(i), value("route" + juce::String(i + 1)));
    setValue("bands", static_cast<float>(count - 1));
}
void GranularProcessor::moveSplit(int index, float frequency)
{
    if (index < 0 || index >= bandCount() - 1) return;
    const auto splits = boundaries();
    const float low = index == 0 ? 30.0f : splits[index - 1] * 1.08f;
    const float high = index == bandCount() - 2 ? displayMaxFrequency() * 0.95f : splits[index + 1] / 1.08f;
    if (low < high) setValue("split" + juce::String(index), juce::jlimit(low, high, frequency));
}
juce::AudioProcessorEditor* GranularProcessor::createEditor() { return new GranularEditor(*this); }
double GranularProcessor::getTailLengthSeconds() const
{
    const int count = juce::jlimit(0, maxModulators, juce::roundToInt(modCount->load()));
    for (int slot = 0; slot < count; ++slot)
        for (int link = 0; link < juce::jlimit(0, maxConnections, juce::roundToInt(modControls[slot].connections->load())); ++link)
            if (juce::roundToInt(modConnections[slot][link].target->load()) == 21
                && juce::jmax(modConnections[slot][link].minimum->load(), modConnections[slot][link].maximum->load()) >= 50)
                return 3600;
    for (int slot = 0; slot < count; ++slot)
        for (int link = 0; link < juce::jlimit(0, maxConnections, juce::roundToInt(modControls[slot].connections->load())); ++link)
            if (juce::roundToInt(modConnections[slot][link].target->load()) == 5
                && juce::jmax(modConnections[slot][link].minimum->load(), modConnections[slot][link].maximum->load()) > 0)
                return 900;
    double tail = 4.5;
    for (int m = 0; m < maxBands; ++m)
    {
        if (playbackControls[m][5]->load() >= 0.5f) return 3600;
        const double amount = feedbackControls[m][0]->load() * 0.01;
        if (amount <= 0) continue;
        if (count > 0) return 900;
        const double roundTrip = feedbackControls[m][1]->load() * 0.001 + 4.25;
        tail = juce::jmax(tail, 4.5 + roundTrip * std::ceil(std::log(0.001) / std::log(juce::jlimit(0.001, 0.95, amount))));
    }
    return tail;
}
void GranularProcessor::getStateInformation(juce::MemoryBlock& destination)
{ if (auto xml = parameters.copyState().createXml()) copyXmlToBinary(*xml, destination); }
void GranularProcessor::setStateInformation(const void* data, int bytes)
{
    if (auto xml = getXmlFromBinary(data, bytes))
        if (xml->hasTagName(parameters.state.getType()))
        {
            auto restored = parameters.copyState();
            const auto incoming = juce::ValueTree::fromXml(*xml);
            for (int i = 0; i < incoming.getNumChildren(); ++i)
            {
                const auto child = incoming.getChild(i);
                auto existing = restored.getChildWithProperty("id", child.getProperty("id"));
                if (existing.isValid()) existing.setProperty("value", child.getProperty("value"), nullptr);
            }
            // Older sessions stored a single symmetric shape. Preserve both slopes.
            for (int m = 0; m < maxBands; ++m)
            {
                if (!incoming.getChildWithProperty("id", "modCount").isValid())
                    restored.getChildWithProperty("id", "modCount").setProperty("value", 0, nullptr);
                restored.getChildWithProperty("id", moduleID(m, "freeze")).setProperty("value", 0, nullptr);
                const std::array<float, 5> playbackDefaults {150, 200, 0, 0, 0};
                for (int i = 0; i < 5; ++i)
                    if (!incoming.getChildWithProperty("id", moduleID(m, modTargets[16 + i])).isValid())
                        restored.getChildWithProperty("id", moduleID(m, modTargets[16 + i])).setProperty("value", playbackDefaults[i], nullptr);
                for (const auto* id : {"feedback", "repeat", "damping"})
                    if (!incoming.getChildWithProperty("id", moduleID(m, id)).isValid())
                        restored.getChildWithProperty("id", moduleID(m, id)).setProperty("value",
                            juce::String(id) == "feedback" ? 0.0f : juce::String(id) == "repeat" ? 350.0f : 6000.0f, nullptr);
                if (!incoming.getChildWithProperty("id", moduleID(m, "release")).isValid())
                {
                    const auto oldShape = incoming.getChildWithProperty("id", moduleID(m, "shape"));
                    if (oldShape.isValid())
                        restored.getChildWithProperty("id", moduleID(m, "release"))
                            .setProperty("value", oldShape.getProperty("value"), nullptr);
                }
                for (int i = 0; i < 4; ++i)
                {
                    const auto id = moduleID(m, "chance" + juce::String(i));
                    if (!incoming.getChildWithProperty("id", id).isValid())
                        restored.getChildWithProperty("id", id).setProperty("value", 0.0f, nullptr);
                }
            }
            parameters.replaceState(restored);
            auditionSource.store(-1);
        }
}
juce::AudioProcessorValueTreeState::ParameterLayout GranularProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto addModule = [&](int m)
    {
        const juce::String prefix = m == 0 ? "" : "M" + juce::String(m + 1) + " ";
        auto add = [&](const char* id, const char* name, float low, float high, float step, float initial, const char* unit)
        {
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{moduleID(m, id), 1},
                prefix + name, juce::NormalisableRange<float>(low, high, step), initial,
                juce::AudioParameterFloatAttributes().withLabel(unit)));
        };
        add("size", "Grain size", 20, 250, 1, 100, "ms");
        add("density", "Density", 2, 40, 0.1f, 15, "grains/s");
        add("mix", "Dry / Wet", 0, 100, 0.1f, 50, "%");
        add("shape", "Grain shape", 0, 100, 0.1f, 0, "%");
    };
    auto addSplitParameter = [&](int i)
    {
        const float initial = i < 3 ? std::array<float, 3>{250, 1500, 6000}[i]
            : 30.0f * std::pow(600.0f, static_cast<float>(i) / (maxSplits - 1));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"split" + juce::String(i), 1},
            "Split " + juce::String(i + 1), juce::NormalisableRange<float>(30, 20000, 1, 0.25f), initial,
            juce::AudioParameterFloatAttributes().withLabel("Hz")));
    };
    juce::StringArray destinations;
    for (int m = 0; m < maxBands; ++m) destinations.add("Module " + juce::String(m + 1));
    auto addRoute = [&](int i)
    {
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"route" + juce::String(i), 1},
            "Band " + juce::String(i + 1) + " route", destinations, i));
    };
    auto addRelease = [&](int m)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{moduleID(m, "release"), 1},
            "M" + juce::String(m + 1) + " Grain release", juce::NormalisableRange<float>(0, 100, 0.1f), 0.0f));
    };
    auto addPitch = [&](int m)
    {
        for (int i = 0; i < 4; ++i)
        {
            layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{moduleID(m, "pitch" + juce::String(i)), 1},
                "M" + juce::String(m + 1) + " Pitch " + juce::String(i + 1), -24, 24, std::array<int, 4>{-12, -7, 7, 12}[i]));
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{moduleID(m, "chance" + juce::String(i)), 1},
                "M" + juce::String(m + 1) + " Chance " + juce::String(i + 1), juce::NormalisableRange<float>(0, 100, 0.1f), 0.0f));
        }
    };
    for (int m = 0; m < 4; ++m) addModule(m);
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"bands", 1}, "Band count", 1, maxBands, 1));
    for (int i = 0; i < 3; ++i) addSplitParameter(i);
    for (int i = 0; i < 4; ++i) addRoute(i);
    for (int m = 0; m < 4; ++m) addRelease(m);
    for (int m = 0; m < 4; ++m) addPitch(m);
    for (int m = 4; m < maxBands; ++m) { addModule(m); addRelease(m); addPitch(m); }
    for (int i = 3; i < maxSplits; ++i) addSplitParameter(i);
    for (int i = 4; i < maxBands; ++i) addRoute(i);
    juce::StringArray targets {"Grain size", "Density", "Dry / Wet", "Attack slope", "Release slope", "Feedback", "Repeat time", "Damping",
        "Pitch 1", "Pitch 2", "Pitch 3", "Pitch 4", "Chance 1", "Chance 2", "Chance 3", "Chance 4",
        "Position", "Spray", "Base pitch", "Reverse chance", "Stereo spread", "Freeze"};
    {
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"modCount", 1},
            "Modulators", 0, maxModulators, 0));
        for (int slot = 0; slot < maxModulators; ++slot)
        {
            const juce::String prefix = "LFO" + juce::String(slot + 1) + " ";
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{modID(slot, "wave"), 1}, prefix + "Wave",
                juce::StringArray{"Sine", "Triangle", "Ramp up", "Ramp down", "Square", "Random hold"}, 0));
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{modID(slot, "rate"), 1}, prefix + "Rate",
                juce::NormalisableRange<float>(0.02f, 20, 0.01f, 0.35f), 0.25f));
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{modID(slot, "sync"), 1}, prefix + "Timing",
                juce::StringArray{"Hz", "1/16", "1/8", "1/4", "1/2", "4 beats", "8 beats", "16 beats"}, 0));
            layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{modID(slot, "connections"), 1}, prefix + "Connections", 0, maxConnections, 0));
            for (int link = 0; link < maxConnections; ++link)
            {
                const auto name = prefix + "Link" + juce::String(link + 1) + " ";
                layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{connectionID(slot, link, "module"), 1}, name + "Module", 0, maxBands - 1, 0));
                layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{connectionID(slot, link, "target"), 1}, name + "Target", targets, 2));
                for (const auto* endpoint : {"min", "max"})
                    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{connectionID(slot, link, endpoint), 1}, name + endpoint,
                        juce::NormalisableRange<float>(0, 100, 0.01f), juce::String(endpoint) == "min" ? 0.0f : 100.0f));
            }
        }
    }
    for (int m = 0; m < maxBands; ++m)
    {
        const juce::String prefix = "M" + juce::String(m + 1) + " ";
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{moduleID(m, "feedback"), 1},
            prefix + "Feedback", juce::NormalisableRange<float>(0, 95, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("%")));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{moduleID(m, "repeat"), 1},
            prefix + "Repeat time", juce::NormalisableRange<float>(10, 2000, 1, 0.4f), 350.0f,
            juce::AudioParameterFloatAttributes().withLabel("ms")));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{moduleID(m, "damping"), 1},
            prefix + "Damping", juce::NormalisableRange<float>(100, 20000, 1, 0.3f), 6000.0f,
            juce::AudioParameterFloatAttributes().withLabel("Hz")));
    }
    for (int m = 0; m < maxBands; ++m)
    {
        const juce::String prefix = "M" + juce::String(m + 1) + " ";
        auto add = [&](const char* id, const char* name, float low, float high, float initial, const char* unit)
        {
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{moduleID(m, id), 1}, prefix + name,
                juce::NormalisableRange<float>(low, high, 0.1f), initial, juce::AudioParameterFloatAttributes().withLabel(unit)));
        };
        add("position", "Position", 0, 1500, 150, "ms");
        add("spray", "Spray", 0, 1000, 200, "ms");
        add("pitch", "Pitch", -24, 24, 0, "st");
        add("reverse", "Reverse chance", 0, 100, 0, "%");
        add("spread", "Stereo spread", 0, 100, 0, "%");
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{moduleID(m, "freeze"), 1}, prefix + "Freeze", false));
    }
    return layout;
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GranularProcessor(); }

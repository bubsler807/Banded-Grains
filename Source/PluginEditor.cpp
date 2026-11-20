#include "PluginEditor.h"

namespace
{
const juce::Colour paper(0xfff4f0e6), ink(0xff272c2b), muted(0xff74766d), rule(0xffc8c5b8);
constexpr int maxBands = GranularProcessor::maxBands;
const std::array<juce::Colour, maxBands> bandColours {juce::Colour(0xff416d76), juce::Colour(0xff927042),
                                        juce::Colour(0xff697849), juce::Colour(0xff87626e), juce::Colour(0xff53678e), juce::Colour(0xffa35f47),
    juce::Colour(0xff497c64), juce::Colour(0xff7b6592), juce::Colour(0xff8a7947),
    juce::Colour(0xff427d87), juce::Colour(0xff946a66), juce::Colour(0xff68754e)};
juce::Font serif(float height) { return juce::Font(juce::FontOptions(juce::Font::getDefaultSerifFontName(), height, juce::Font::plain)); }
juce::Font sans(float height) { return juce::Font(juce::FontOptions(height)); }
juce::String frequencyText(float f)
{ return f >= 1000 ? juce::String(f / 1000, f < 10000 ? 1 : 0) + " kHz" : juce::String(juce::roundToInt(f)) + " Hz"; }
}

class GranularEditor::PaperLook final : public juce::LookAndFeel_V4
{
public:
    PaperLook()
    {
        setColour(juce::ComboBox::backgroundColourId, paper.brighter(0.15f));
        setColour(juce::ComboBox::textColourId, ink);
        setColour(juce::ComboBox::outlineColourId, rule);
        setColour(juce::ComboBox::arrowColourId, ink);
        setColour(juce::PopupMenu::backgroundColourId, paper);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, bandColours[0]);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour(juce::Slider::textBoxTextColourId, ink);
        setColour(juce::Slider::textBoxOutlineColourId, rule);
        setColour(juce::Slider::textBoxBackgroundColourId, paper);
        setColour(juce::Slider::textBoxHighlightColourId, bandColours[0].withAlpha(0.25f));
        setColour(juce::ScrollBar::thumbColourId, muted.withAlpha(0.7f));
        setColour(juce::ScrollBar::backgroundColourId, paper);
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                          float, float, juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const float centre = y + height * 0.5f;
        g.setColour(rule); g.drawLine(static_cast<float>(x), centre, static_cast<float>(x + width), centre, 1);
        g.setColour(slider.findColour(juce::Slider::trackColourId));
        g.drawLine(static_cast<float>(x), centre, position, centre, 2);
        g.fillRect(position - 3, centre - 7, 6.0f, 14.0f);
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool hover, bool) override
    {
        g.setColour(button.getToggleState() ? ink : (hover ? paper.darker(0.04f) : paper));
        g.fillRect(button.getLocalBounds());
        g.setColour(rule); g.drawRect(button.getLocalBounds());
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        const bool solo = button.getComponentID().startsWith("listen") || button.getComponentID().startsWith("moduleSolo");
        const bool module = !solo && button.getComponentID().startsWith("module");
        g.setFont(solo ? sans(juce::jmin(15.0f, button.getHeight() * 0.68f)) : serif(module ? 14.0f : 17.0f));
        g.setColour(button.getToggleState() ? (solo ? juce::Colours::white : paper) : ink);
        if (module)
            g.drawFittedText(button.getButtonText().replace("Module ", "M"), button.getLocalBounds().withTrimmedRight(37).reduced(5, 0),
                             juce::Justification::centredLeft, 1, 0.85f);
        else
            g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }
};

class GranularEditor::Spectrum final : public juce::Component, private juce::Timer
{
public:
    explicit Spectrum(GranularProcessor& p) : processor(p)
    {
        setComponentID("spectrum");
        setTitle("Input frequency spectrum");
        setDescription("Double-click to add a frequency split. Drag a split to move it. Right-click a split to remove it.");
        decibels.fill(-90);
        startTimerHz(30);
    }
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(42, 24); }
    float xFor(float hz) const
    {
        const auto r = plot();
        return r.getX() + r.getWidth() * std::log(juce::jlimit(20.0f, processor.displayMaxFrequency(), hz) / 20.0f)
            / std::log(processor.displayMaxFrequency() / 20.0f);
    }
    float frequencyFor(float x) const
    {
        const auto r = plot();
        return 20.0f * std::pow(processor.displayMaxFrequency() / 20.0f,
                               juce::jlimit(0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
    }
    int splitAt(float x) const
    {
        const auto splits = processor.boundaries();
        for (int i = 0; i < processor.bandCount() - 1; ++i)
            if (std::abs(x - xFor(splits[i])) < 9) return i;
        return -1;
    }
    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (plot().contains(event.position) && splitAt(event.position.x) < 0)
            processor.addSplit(frequencyFor(event.position.x));
        repaint();
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        dragging = splitAt(event.position.x);
        if (event.mods.isPopupMenu() && dragging >= 0)
        { processor.removeSplit(dragging); dragging = -1; }
    }
    void mouseDrag(const juce::MouseEvent& event) override
    { if (dragging >= 0) { processor.moveSplit(dragging, frequencyFor(event.position.x)); repaint(); } }
    void mouseUp(const juce::MouseEvent&) override { dragging = -1; }
    void mouseMove(const juce::MouseEvent& event) override
    { setMouseCursor(splitAt(event.position.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::CrosshairCursor); }
    void paint(juce::Graphics& g) override
    {
        const auto r = plot();
        const auto splits = processor.boundaries();
        for (int band = 0; band < processor.bandCount(); ++band)
        {
            const float left = band == 0 ? r.getX() : xFor(splits[band - 1]);
            const float right = band == processor.bandCount() - 1 ? r.getRight() : xFor(splits[band]);
            const int module = juce::jlimit(0, maxBands - 1, juce::roundToInt(processor.value("route" + juce::String(band))));
            g.setColour(bandColours[module].withAlpha(0.07f)); g.fillRect(left, r.getY(), right - left, r.getHeight());
            if (right - left > 65)
            {
                g.setColour(bandColours[module]); g.setFont(serif(16));
                g.drawText(juce::String(band + 1) + " / M" + juce::String(module + 1),
                           juce::Rectangle<float>(left, r.getY() + 6, right - left, 22), juce::Justification::centred);
            }
        }
        g.setFont(sans(11));
        for (int db = 0; db >= -90; db -= 30)
        {
            const float y = r.getY() + r.getHeight() * (-db / 90.0f);
            g.setColour(rule.withAlpha(0.55f)); g.drawHorizontalLine(juce::roundToInt(y), r.getX(), r.getRight());
            g.setColour(muted); g.drawText(juce::String(db), 1, juce::roundToInt(y) - 7, 34, 14, juce::Justification::centredRight);
        }
        for (float frequency : {20.0f, 50.0f, 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f, 20000.0f})
        {
            if (frequency > processor.displayMaxFrequency()) continue;
            const float x = xFor(frequency);
            g.setColour(rule.withAlpha(0.45f)); g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
            g.setColour(muted); g.drawText(frequency >= 1000 ? juce::String(frequency / 1000, 0) + "k" : juce::String(frequency, 0),
                                          juce::Rectangle<float>(x - 22, r.getBottom() + 6, 44, 16), juce::Justification::centred);
        }
        juce::Path trace;
        for (int x = 0; x <= juce::roundToInt(r.getWidth()); ++x)
        {
            const float hz = frequencyFor(r.getX() + x);
            const float bin = juce::jlimit(0.0f, static_cast<float>(fftSize / 2 - 1), hz * fftSize / processor.spectrumSampleRate());
            const int lo = static_cast<int>(bin), hi = juce::jmin(lo + 1, fftSize / 2 - 1);
            const float db = decibels[lo] + (bin - lo) * (decibels[hi] - decibels[lo]);
            const float y = r.getBottom() - juce::jlimit(0.0f, 1.0f, (db + 90) / 90) * r.getHeight();
            if (x == 0) trace.startNewSubPath(r.getX(), y); else trace.lineTo(r.getX() + x, y);
        }
        g.setColour(ink.withAlpha(0.85f)); g.strokePath(trace, juce::PathStrokeType(1.3f));
        for (int i = 0; i < processor.bandCount() - 1; ++i)
        {
            const float x = xFor(splits[i]);
            g.setColour(ink); g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
            g.fillRect(x - 3, r.getY() - 5, 6.0f, 10.0f);
            g.setFont(sans(11));
            g.drawText(frequencyText(splits[i]), juce::Rectangle<float>(x - 45, r.getY() - 22, 90, 16), juce::Justification::centred);
        }
        g.setColour(rule); g.drawRect(r);
    }
private:
    void timerCallback() override
    {
        std::array<float, fftSize> left, right;
        bool updated = false;
        for (int batch = 0; batch < 8; ++batch)
        {
            const int count = processor.readSpectrum(left.data(), right.data(), fftSize);
            if (count == 0) break;
            for (int i = 0; i < count; ++i)
            {
                frame[0][filled] = left[i]; frame[1][filled] = right[i];
                if (++filled != fftSize) continue;
                std::array<float, fftSize / 2> power {};
                for (int ch = 0; ch < 2; ++ch)
                {
                    std::array<float, fftSize * 2> data {};
                    std::copy(frame[ch].begin(), frame[ch].end(), data.begin());
                    window.multiplyWithWindowingTable(data.data(), fftSize);
                    fft.performFrequencyOnlyForwardTransform(data.data());
                    for (int bin = 0; bin < fftSize / 2; ++bin) power[bin] += data[bin] * data[bin] * 0.5f;
                }
                for (int bin = 0; bin < fftSize / 2; ++bin)
                {
                    const float db = juce::Decibels::gainToDecibels(std::sqrt(power[bin]) * 4.0f / fftSize, -90.0f);
                    decibels[bin] = juce::jmax(db, decibels[bin] - 2.0f);
                }
                filled = 0; updated = true;
            }
        }
        if (!updated) for (auto& db : decibels) db = juce::jmax(-90.0f, db - 2.0f);
        repaint();
    }
    GranularProcessor& processor;
    static constexpr int fftSize = 2048;
    juce::dsp::FFT fft {11};
    juce::dsp::WindowingFunction<float> window {fftSize, juce::dsp::WindowingFunction<float>::hann, false};
    std::array<std::array<float, fftSize>, 2> frame {};
    std::array<float, fftSize / 2> decibels;
    int filled = 0, dragging = -1;
};

class GranularEditor::Envelope final : public juce::Component
{
public:
    explicit Envelope(GranularProcessor& p) : processor(p)
    {
        setComponentID("envelope");
        setTitle("Grain envelope");
        setDescription("Drag the left slope to set attack and the right slope to set release.");
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    ~Envelope() override { finishGesture(); }
    void select(int m) { finishGesture(); module = m; repaint(); }
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(16, 24); }
    float fade(const char* id) const { return 0.5f - 0.00475f * processor.value(GranularProcessor::moduleID(module, id)); }
    void paint(juce::Graphics& g) override
    {
        const auto r = plot();
        const float attack = fade("shape"), release = fade("release");
        g.setColour(rule); g.drawRect(r);
        juce::Path curve;
        for (int i = 0; i <= 300; ++i)
        {
            const float phase = i / 300.0f;
            const float x = r.getX() + phase * r.getWidth();
            const float y = r.getBottom() - GranularEngine::envelope(phase, attack, release) * r.getHeight();
            if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
        }
        g.setColour(bandColours[module]); g.strokePath(curve, juce::PathStrokeType(2));
        // Mid-slope handles remain separated even with a rounded, symmetric envelope.
        for (float phase : {attack * 0.5f, 1.0f - release * 0.5f})
            g.fillEllipse(r.getX() + phase * r.getWidth() - 5, r.getCentreY() - 5, 10, 10);
        g.setFont(sans(12)); g.setColour(ink);
        g.drawText("Attack " + juce::String(attack * 100, 1) + "%", 16, 0, 200, 20, juce::Justification::centredLeft);
        g.drawText("Release " + juce::String(release * 100, 1) + "%", getWidth() - 216, 0, 200, 20, juce::Justification::centredRight);
        g.setColour(muted); g.setFont(sans(11));
        g.drawText("Drag either slope", 0, getHeight() - 20, getWidth(), 20, juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        finishGesture();
        right = e.position.x >= plot().getCentreX();
        active = processor.parameters.getParameter(GranularProcessor::moduleID(module, right ? "release" : "shape"));
        active->beginChangeGesture();
        startX = e.position.x;
        startFade = fade(right ? "release" : "shape");
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (active == nullptr) return;
        const float next = juce::jlimit(0.025f, 0.5f, startFade + (right ? -2 : 2) * (e.position.x - startX) / plot().getWidth());
        active->setValueNotifyingHost(active->convertTo0to1((0.5f - next) / 0.00475f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { finishGesture(); }
private:
    void finishGesture() { if (active != nullptr) { active->endChangeGesture(); active = nullptr; } }
    GranularProcessor& processor;
    juce::RangedAudioParameter* active = nullptr;
    int module = 0;
    bool right = false;
    float startX = 0, startFade = 0;
};

class GranularEditor::GrainView final : public juce::Component
{
public:
    explicit GrainView(GranularProcessor& p) : processor(p) { setComponentID("grainVisualizer"); setTitle("Live grain playback"); }
    void select(int m) { module = m; repaint(); }
    void update() { processor.readGrainVisuals(states); repaint(); }
    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().withTrimmedLeft(145).reduced(0, 3);
        g.setFont(sans(11)); g.setColour(ink);
        g.drawText("Grains / Module " + juce::String(module + 1), 0, 2, 140, 18, juce::Justification::centredLeft);
        g.setColour(muted); g.setFont(sans(10));
        g.drawText("4 s buffer / line = record", 0, 21, 140, 17, juce::Justification::centredLeft);
        g.setColour(rule); g.drawRect(r);
        for (int i = 1; i < 4; ++i) g.drawVerticalLine(juce::roundToInt(r.getX() + r.getWidth() * i / 4), r.getY(), r.getBottom());
        const auto& state = states[module];
        g.setColour(ink.withAlpha(0.65f));
        const float recordX = r.getX() + state.recordHead * r.getWidth();
        g.drawLine(recordX, r.getY(), recordX, r.getBottom(), 1.5f);
        for (size_t i = 0; i < state.grains.size(); ++i)
        {
            const auto& grain = state.grains[i];
            if (grain.speed == 0) continue;
            const float x = r.getX() + grain.position * r.getWidth(), y = r.getY() + 4 + (i % 6) * (r.getHeight() - 8) / 6;
            const float direction = grain.speed < 0 ? -1.0f : 1.0f;
            g.setColour(bandColours[module].withAlpha(0.5f + 0.5f * grain.amplitude));
            g.drawLine(juce::jlimit(r.getX(), r.getRight(), x - direction * 22), y, x, y, 2.5f);
            g.fillEllipse(x - 3, y - 3, 6, 6);
            g.drawLine(x, y, x - direction * 4, y - 3, 1);
        }
    }
private:
    GranularProcessor& processor;
    GranularProcessor::GrainVisuals states {};
    int module = 0;
};

class GranularEditor::Socket final : public juce::Component
{
public:
    Socket(GranularEditor& e, int index, bool output) : owner(e), number(index), isOutput(output)
    {
        setComponentID((output ? "modOutput" : "modInput") + juce::String(index));
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTitle(output ? "Modulator output " + juce::String(index + 1) : "Modulation input " + juce::String(index + 1));
    }
    void paint(juce::Graphics& g) override
    {
        g.setColour(isMouseOver() ? ink : muted);
        g.drawEllipse(getLocalBounds().toFloat().reduced(3), 1.5f);
        if (isOutput)
        {
            g.setColour(bandColours[owner.selectedModule].withAlpha(0.3f + 0.7f * owner.processor.modulationOutput(number)));
            g.fillEllipse(getLocalBounds().toFloat().reduced(6));
        }
    }
    void mouseEnter(const juce::MouseEvent&) override { if (isOutput) owner.hoveredSource = number; else owner.hoveredTarget = number; owner.repaint(); }
    void mouseExit(const juce::MouseEvent&) override { if (isOutput) owner.hoveredSource = -1; else owner.hoveredTarget = -1; owner.repaint(); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!isOutput)
        {
            for (int source = 0; source < owner.shownModCount; ++source)
            {
                const int links = juce::roundToInt(owner.processor.value(GranularProcessor::modID(source, "connections")));
                for (int link = 0; link < links; ++link)
                    if (juce::roundToInt(owner.processor.value(GranularProcessor::connectionID(source, link, "module"))) == owner.selectedModule
                        && juce::roundToInt(owner.processor.value(GranularProcessor::connectionID(source, link, "target"))) == number)
                    {
                        owner.selectedSource = source; owner.refreshModulation();
                        owner.connectionViewport.setViewPosition(0, link * 38); return;
                    }
            }
            return;
        }
        owner.selectedSource = number;
        owner.refreshModulation();
        owner.draggingSource = number;
        owner.wireEnd = e.getEventRelativeTo(&owner).position;
        owner.repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!isOutput) return;
        owner.wireEnd = e.getEventRelativeTo(&owner).position;
        for (int module = 0; module < GranularProcessor::maxBands; ++module)
            if (module != owner.selectedModule && owner.tabs[module].getBounds().toFloat().contains(owner.wireEnd))
            { owner.selectModule(module); break; }
        if (owner.grainsPage.getBounds().toFloat().contains(owner.wireEnd)) owner.showPlayback(false);
        if (owner.playbackPage.getBounds().toFloat().contains(owner.wireEnd)) owner.showPlayback(true);
        owner.repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!isOutput || owner.draggingSource < 0) return;
        const auto end = e.getEventRelativeTo(&owner).position;
        for (int target = 0; target < GranularProcessor::modTargetCount; ++target)
            if (owner.inputSockets[target]->isVisible()
                && owner.inputSockets[target]->getBounds().toFloat().expanded(7).contains(end))
            { owner.connectModulator(number, target); break; }
        owner.draggingSource = -1;
        owner.repaint();
    }
private:
    GranularEditor& owner;
    int number;
    bool isOutput;
};

GranularEditor::GranularEditor(GranularProcessor& p)
    : AudioProcessorEditor(p), processor(p), paperLook(std::make_unique<PaperLook>()),
      spectrum(std::make_unique<Spectrum>(p)), envelope(std::make_unique<Envelope>(p)), grainView(std::make_unique<GrainView>(p))
{
    setLookAndFeel(paperLook.get());
    addAndMakeVisible(*spectrum);
    addAndMakeVisible(*envelope);
    addAndMakeVisible(*grainView);
    grainsPage.setComponentID("grainsPage"); playbackPage.setComponentID("playbackPage");
    grainsPage.onClick = [this] { showPlayback(false); };
    playbackPage.onClick = [this] { showPlayback(true); };
    addAndMakeVisible(grainsPage); addAndMakeVisible(playbackPage);
    freezeButton.setClickingTogglesState(true); freezeButton.setComponentID("freeze");
    addAndMakeVisible(freezeButton);
    for (int i = 0; i < 5; ++i)
    {
        auto& slider = playbackSliders[i];
        slider.setComponentID(GranularProcessor::modTargets[16 + i]);
        slider.setTitle(juce::StringArray{"Position", "Spray", "Base pitch", "Reverse chance", "Stereo spread"}[i]);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 85, 24);
        slider.setTextValueSuffix(juce::StringArray{" ms", " ms", " st", " %", " %"}[i]);
        addAndMakeVisible(slider);
    }
    bandViewport.setViewedComponent(&bandStrip, false);
    bandViewport.setScrollBarsShown(false, true);
    bandViewport.setScrollBarThickness(12);
    addAndMakeVisible(bandViewport);
    juce::StringArray destinations;
    for (int m = 0; m < maxBands; ++m) destinations.add("Module " + juce::String(m + 1));
    for (int i = 0; i < maxBands; ++i)
    {
        bandLabels[i].setFont(sans(11));
        bandLabels[i].setColour(juce::Label::textColourId, ink);
        bandStrip.addAndMakeVisible(bandLabels[i]);
        routes[i].addItemList(destinations, 1);
        routes[i].setComponentID("route" + juce::String(i));
        routes[i].setTitle("Band " + juce::String(i + 1) + " destination");
        bandStrip.addAndMakeVisible(routes[i]);
        listenButtons[i].setButtonText("s");
        listenButtons[i].setComponentID("listen" + juce::String(i));
        listenButtons[i].setTitle("Listen to band " + juce::String(i + 1));
        listenButtons[i].setTooltip("Hear this frequency band before granular processing. Click again to hear the full output.");
        listenButtons[i].onClick = [this, i]
        {
            processor.toggleBandListen(i);
            timerCallback();
        };
        addAndMakeVisible(listenButtons[i]);
        routeAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            p.parameters, "route" + juce::String(i), routes[i]);
        routes[i].onChange = [this, i] { if (routes[i].getSelectedItemIndex() >= 0) selectModule(routes[i].getSelectedItemIndex()); };
        tabs[i].setButtonText("Module " + juce::String(i + 1));
        tabs[i].setComponentID("module" + juce::String(i));
        tabs[i].onClick = [this, i] { selectModule(i); };
        addAndMakeVisible(tabs[i]);
        moduleSoloButtons[i].setButtonText("s");
        moduleSoloButtons[i].setComponentID("moduleSolo" + juce::String(i));
        moduleSoloButtons[i].setTitle("Solo module " + juce::String(i + 1));
        moduleSoloButtons[i].setTooltip("Hear only this module's processed output, including grains and feedback. Click again for the full output.");
        moduleSoloButtons[i].onClick = [this, i] { processor.toggleModuleSolo(i); timerCallback(); };
        addAndMakeVisible(moduleSoloButtons[i]);
        if (i >= 3) continue;
        sliders[i].setSliderStyle(juce::Slider::LinearHorizontal);
        sliders[i].setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 24);
        sliders[i].setComponentID(juce::StringArray{"size", "density", "mix", "shape"}[i]);
        sliders[i].setTitle(juce::StringArray{"Grain size", "Density", "Dry / Wet", "Grain shape"}[i]);
        addAndMakeVisible(sliders[i]);
    }
    for (int i = 0; i < 4; ++i)
    {
        for (auto* slider : { &pitchSteps[i], &pitchChances[i] })
        {
            slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 24);
            addAndMakeVisible(*slider);
        }
        pitchSteps[i].setComponentID("pitch" + juce::String(i));
        pitchSteps[i].setTitle("Pitch choice " + juce::String(i + 1) + " semitones");
        pitchChances[i].setComponentID("chance" + juce::String(i));
        pitchChances[i].setTitle("Pitch choice " + juce::String(i + 1) + " probability");
        pitchChances[i].setTextValueSuffix(" %");
    }
    for (int i = 0; i < 3; ++i)
    {
        feedbackSliders[i].setSliderStyle(juce::Slider::LinearHorizontal);
        feedbackSliders[i].setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
        feedbackSliders[i].setComponentID(juce::StringArray{"feedback", "repeat", "damping"}[i]);
        feedbackSliders[i].setTitle(juce::StringArray{"Feedback amount", "Feedback repeat time", "Feedback damping cutoff"}[i]);
        feedbackSliders[i].setTextValueSuffix(juce::StringArray{" %", " ms", " Hz"}[i]);
        feedbackSliders[i].setTooltip(juce::StringArray{
            "Return wet grains to the buffer. Independent of Dry / Wet.",
            "Delay before wet grains return to the buffer. Grain playback adds its own latency.",
            "Low-pass cutoff inside the feedback loop. Lower values darken successive repeats."}[i]);
        addAndMakeVisible(feedbackSliders[i]);
    }
    for (auto* combo : {&sourceSelector, &modWave, &modSync}) addAndMakeVisible(*combo);
    modWave.addItemList({"Sine", "Triangle", "Ramp up", "Ramp down", "Square", "Random hold"}, 1);
    modSync.addItemList({"Hz", "1/16", "1/8", "1/4", "1/2", "4 beats", "8 beats", "16 beats"}, 1);
    modRate.setSliderStyle(juce::Slider::LinearHorizontal);
    modRate.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 24);
    modRate.setTextValueSuffix(" Hz");
    modRate.setColour(juce::Slider::textBoxTextColourId, ink);
    modRate.setColour(juce::Slider::textBoxBackgroundColourId, paper);
    modRate.setColour(juce::Slider::textBoxOutlineColourId, rule);
    addAndMakeVisible(modRate);
    sourceSelector.onChange = [this] { if (sourceSelector.getSelectedId() > 0) { selectedSource = sourceSelector.getSelectedId() - 1; refreshModulation(); } };
    addAndMakeVisible(addModulator); addAndMakeVisible(removeModulator);
    addModulator.setComponentID("addModulator");
    removeModulator.setComponentID("removeModulator");
    addModulator.onClick = [this]
    {
        const int count = juce::roundToInt(processor.value("modCount"));
        if (count == GranularProcessor::maxModulators) return;
        selectedSource = count;
        for (const auto* id : {"wave", "sync", "connections"}) processor.setValue(GranularProcessor::modID(count, id), 0);
        processor.setValue(GranularProcessor::modID(count, "rate"), 0.25f);
        processor.setValue("modCount", static_cast<float>(count + 1));
        patchMessage = "Drag an output socket to any control input.";
        refreshModulation();
    };
    removeModulator.onClick = [this]
    {
        const int count = juce::roundToInt(processor.value("modCount"));
        if (count == 0) return;
        for (int slot = selectedSource; slot < count - 1; ++slot)
        {
            for (const auto* id : {"wave", "rate", "sync", "connections"})
                processor.setValue(GranularProcessor::modID(slot, id), processor.value(GranularProcessor::modID(slot + 1, id)));
            for (int link = 0; link < GranularProcessor::maxConnections; ++link)
                for (const auto* id : {"module", "target", "min", "max"})
                    processor.setValue(GranularProcessor::connectionID(slot, link, id), processor.value(GranularProcessor::connectionID(slot + 1, link, id)));
        }
        processor.setValue("modCount", static_cast<float>(count - 1));
        selectedSource = juce::jmax(0, selectedSource - 1); hoveredSource = -1; draggingSource = -1;
        refreshModulation();
    };
    connectionViewport.setViewedComponent(&connectionStrip, false);
    connectionViewport.setScrollBarsShown(true, false);
    connectionViewport.setScrollBarThickness(10);
    addAndMakeVisible(connectionViewport);
    for (int link = 0; link < GranularProcessor::maxConnections; ++link)
    {
        auto& row = connectionRows[link];
        for (int module = 0; module < maxBands; ++module) row.module.addItem("Module " + juce::String(module + 1), module + 1);
        connectionStrip.addAndMakeVisible(row.module);
        row.module.onChange = [this, link] { updateRangeText(link); repaint(); };
        row.target.addItemList({"Grain size", "Density", "Dry / Wet", "Attack slope", "Release slope", "Feedback", "Repeat time", "Damping",
            "Pitch 1", "Pitch 2", "Pitch 3", "Pitch 4", "Chance 1", "Chance 2", "Chance 3", "Chance 4",
            "Position", "Spray", "Base pitch", "Reverse chance", "Stereo spread", "Freeze"}, 1);
        row.target.setComponentID("connectionTarget" + juce::String(link));
        connectionStrip.addAndMakeVisible(row.target);
        for (auto* slider : {&row.minimum, &row.maximum})
        {
            slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 95, 24);
            slider->setColour(juce::Slider::textBoxTextColourId, ink);
            slider->setColour(juce::Slider::textBoxBackgroundColourId, paper);
            slider->setColour(juce::Slider::textBoxOutlineColourId, rule);
            connectionStrip.addAndMakeVisible(*slider);
        }
        row.minimum.setComponentID("connectionMin" + juce::String(link));
        row.maximum.setComponentID("connectionMax" + juce::String(link));
        row.target.onChange = [this, link] { updateRangeText(link); repaint(); };
        row.remove.onClick = [this, link] { removeConnection(link); };
        connectionStrip.addAndMakeVisible(row.remove);
    }
    for (int i = 0; i < GranularProcessor::modTargetCount; ++i)
    {
        inputSockets[i] = std::make_unique<Socket>(*this, i, false);
        inputSockets[i]->setTitle("Modulation input: " + juce::String(GranularProcessor::modTargets[i]));
        addAndMakeVisible(*inputSockets[i]);
    }
    for (int i = 0; i < GranularProcessor::maxModulators; ++i)
    {
        outputSockets[i] = std::make_unique<Socket>(*this, i, true);
        addAndMakeVisible(*outputSockets[i]);
    }
    selectModule(0);
    setSize(1100, 1000);
    showPlayback(false);
    timerCallback();
    startTimerHz(30);
}
GranularEditor::~GranularEditor() { stopTimer(); setLookAndFeel(nullptr); }
void GranularEditor::selectModule(int module)
{
    selectedModule = juce::jlimit(0, maxBands - 1, module);
    if (draggingSource < 0) { hoveredSource = -1; hoveredTarget = -1; }
    refreshModulation();
    for (int band = 0; band < processor.bandCount(); ++band)
        if (juce::roundToInt(processor.value("route" + juce::String(band))) == selectedModule)
        {
            bandViewport.setViewPosition(juce::jmax(0, band - 3) * ((getWidth() - 64) / 4), 0);
            break;
        }
    for (auto& attachment : playbackAttachments) attachment.reset();
    freezeAttachment.reset();
    for (int i = 0; i < 5; ++i)
    {
        playbackAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters,
            GranularProcessor::moduleID(selectedModule, GranularProcessor::modTargets[16 + i]), playbackSliders[i]);
        playbackSliders[i].setColour(juce::Slider::trackColourId, bandColours[selectedModule]);
    }
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.parameters,
        GranularProcessor::moduleID(selectedModule, "freeze"), freezeButton);
    grainView->select(selectedModule);
    for (auto& attachment : attachments) attachment.reset();
    for (auto& attachment : feedbackAttachments) attachment.reset();
    for (int i = 0; i < 3; ++i)
    {
        feedbackAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters,
            GranularProcessor::moduleID(selectedModule, juce::StringArray{"feedback", "repeat", "damping"}[i]), feedbackSliders[i]);
        feedbackSliders[i].setColour(juce::Slider::trackColourId, bandColours[selectedModule]);
    }
    for (auto& attachment : stepAttachments) attachment.reset();
    for (auto& attachment : chanceAttachments) attachment.reset();
    for (int i = 0; i < 4; ++i)
    {
        stepAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters, GranularProcessor::moduleID(selectedModule, "pitch" + juce::String(i)), pitchSteps[i]);
        chanceAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters, GranularProcessor::moduleID(selectedModule, "chance" + juce::String(i)), pitchChances[i]);
        pitchChances[i].onValueChange = [this] { repaint(); };
        pitchSteps[i].setColour(juce::Slider::trackColourId, bandColours[selectedModule]);
        pitchChances[i].setColour(juce::Slider::trackColourId, bandColours[selectedModule]);
    }
    const juce::StringArray ids {"size", "density", "mix", "shape"};
    envelope->select(selectedModule);
    for (int i = 0; i < maxBands; ++i)
    {
        tabs[i].setToggleState(i == selectedModule, juce::dontSendNotification);
        if (i >= 3) continue;
        sliders[i].setColour(juce::Slider::trackColourId, bandColours[selectedModule]);
        sliders[i].setTextValueSuffix(juce::StringArray{" ms", " /s", " %", " %"}[i]);
        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.parameters, GranularProcessor::moduleID(selectedModule, ids[i]), sliders[i]);
    }
    repaint();
}
void GranularEditor::resized()
{
    spectrum->setBounds(0, 78, getWidth(), 220);
    const int width = (getWidth() - 64) / 4;
    bandViewport.setBounds(32, 326, getWidth() - 64, 74);
    bandStrip.setSize(juce::jmax(4, processor.bandCount()) * width, 60);
    for (int i = 0; i < maxBands; ++i)
    {
        const int x = i * width;
        bandLabels[i].setBounds(x + 6, 0, width - 12, 20);
        routes[i].setBounds(x + 10, 22, width - 20, 28);
        const int left = 32 + i * (getWidth() - 64) / maxBands;
        const int right = 32 + (i + 1) * (getWidth() - 64) / maxBands;
        tabs[i].setBounds(left, 407, right - left, 22);
        moduleSoloButtons[i].setBounds(right - 35, 409, 18, 18);
        if (i < 3) sliders[i].setBounds(132, 480 + i * 44, 175, 38);
    }
    grainsPage.setBounds(42, 450, 105, 24); playbackPage.setBounds(150, 450, 115, 24);
    for (int i = 0; i < 5; ++i)
    {
        const int column = i / 2, row = i % 2;
        const int x = 42 + column * 354;
        playbackSliders[i].setBounds(x + 20, 512 + row * 62, 285, 30);
        inputSockets[16 + i]->setBounds(x, 518 + row * 62, 18, 18);
    }
    freezeButton.setBounds(770, 574, 270, 30); inputSockets[21]->setBounds(750, 580, 18, 18);
    grainView->setBounds(42, 617, getWidth() - 84, 44);
    sourceSelector.setBounds(180, 852, 125, 28);
    addModulator.setBounds(318, 852, 140, 28);
    removeModulator.setBounds(470, 852, 90, 28);
    modWave.setBounds(42, 904, 155, 28);
    modRate.setBounds(210, 904, 180, 28);
    modSync.setBounds(405, 904, 110, 28);
    connectionViewport.setBounds(42, 957, getWidth() - 84, 35);
    connectionStrip.setSize(getWidth() - 100, juce::jmax(1, shownConnections) * 38);
    for (int link = 0; link < GranularProcessor::maxConnections; ++link)
    {
        auto& row = connectionRows[link];
        row.module.setBounds(0, link * 38, 120, 28);
        row.target.setBounds(130, link * 38, 175, 28);
        row.minimum.setBounds(320, link * 38, 250, 28);
        row.maximum.setBounds(585, link * 38, 250, 28);
        row.remove.setBounds(850, link * 38, 105, 28);
    }
    const std::array<juce::Point<int>, 8> sockets {{{122, 499}, {122, 543}, {122, 587}, {346, 490},
        {getWidth() - 386, 490}, {getWidth() - 250, 499}, {getWidth() - 250, 543}, {getWidth() - 250, 587}}};
    for (int i = 0; i < 8; ++i) inputSockets[i]->setBounds(sockets[i].x - 9, sockets[i].y - 9, 18, 18);
    for (int i = 0; i < 4; ++i)
    {
        inputSockets[8 + i]->setBounds(32 + i * width, 732, 18, 18);
        inputSockets[12 + i]->setBounds(32 + i * width, 785, 18, 18);
        outputSockets[i]->setBounds(710 + i * 90, 855, 22, 22);
    }
    envelope->setBounds(340, 478, getWidth() - 720, 132);
    for (int i = 0; i < 3; ++i) feedbackSliders[i].setBounds(getWidth() - 238, 480 + i * 44, 196, 38);
    for (int i = 0; i < 4; ++i)
    {
        pitchSteps[i].setBounds(42 + i * width, 725, width - 20, 32);
        pitchChances[i].setBounds(42 + i * width, 778, width - 20, 32);
    }
}
void GranularEditor::timerCallback()
{
    const int count = processor.bandCount();
    if (count != displayedBands)
    {
        displayedBands = count;
        bandStrip.setSize(juce::jmax(4, count) * ((getWidth() - 64) / 4), 60);
    }
    const auto splits = processor.boundaries();
    for (int i = 0; i < maxBands; ++i)
    {
        const bool active = i < count;
        routes[i].setVisible(active);
        listenButtons[i].setVisible(active);
        bandLabels[i].setVisible(active);
        if (active)
        {
            const float low = i == 0 ? 20 : splits[i - 1];
            const float high = i == count - 1 ? processor.displayMaxFrequency() : splits[i];
            bandLabels[i].setText("Band " + juce::String(i + 1) + "  /  " + frequencyText(low) + " - " + frequencyText(high), juce::dontSendNotification);
            const auto xFor = [this](float hz)
            {
                return 42.0f + (getWidth() - 84.0f) * std::log(hz / 20.0f) / std::log(processor.displayMaxFrequency() / 20.0f);
            };
            const float left = xFor(juce::jlimit(20.0f, processor.displayMaxFrequency(), low));
            const float right = xFor(juce::jlimit(20.0f, processor.displayMaxFrequency(), high));
            const int buttonSize = juce::jlimit(10, 22, juce::roundToInt(right - left - 4));
            listenButtons[i].setBounds(juce::roundToInt((left + right - buttonSize) * 0.5f), 299 + (22 - buttonSize) / 2, buttonSize, buttonSize);
        }
        listenButtons[i].setToggleState(processor.listenedBand() == i, juce::dontSendNotification);
        moduleSoloButtons[i].setToggleState(processor.soloedModule() == i, juce::dontSendNotification);
    }
    const int modCount = juce::roundToInt(processor.value("modCount"));
    const int links = modCount > 0 ? juce::roundToInt(processor.value(GranularProcessor::modID(juce::jmin(selectedSource, modCount - 1), "connections"))) : 0;
    if (modCount != shownModCount || links != shownConnections) refreshModulation();
    modRate.setEnabled(modCount > 0 && modSync.getSelectedItemIndex() == 0);
    envelope->repaint();
    grainView->update();
    repaint();
}
void GranularEditor::paint(juce::Graphics& g)
{
    g.fillAll(paper);
    g.setColour(ink); g.setFont(serif(34));
    g.drawText("Banded Grains", 32, 18, getWidth() - 64, 46, juce::Justification::centredLeft);
    g.setColour(rule); g.drawHorizontalLine(76, 32, static_cast<float>(getWidth() - 32));
    const int width = (getWidth() - 64) / 4;
    g.setColour(rule); g.drawRect(32, 326, getWidth() - 64, 60);
    if (!playbackVisible)
    {
    for (int i = 0; i < 3; ++i)
    {
        g.setColour(ink); g.setFont(serif(18));
        g.drawText(juce::StringArray{"Grain size", "Density", "Dry / Wet"}[i], 42, 485 + i * 44, 90, 24, juce::Justification::centredLeft);
    }
    g.setColour(ink); g.setFont(serif(18));
    g.drawText("Grain envelope", 356, 450, 350, 24, juce::Justification::centredLeft);
    g.drawText("Feedback loop", getWidth() - 340, 450, 300, 24, juce::Justification::centredLeft);
    for (int i = 0; i < 3; ++i)
        g.drawText(juce::StringArray{"Feedback", "Repeat time", "Damping"}[i], getWidth() - 340, 485 + i * 44, 102, 24, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour(ink); g.setFont(serif(17));
        for (int i = 0; i < 6; ++i)
            g.drawText(juce::StringArray{"Position", "Spray", "Base pitch", "Reverse chance", "Stereo spread", "Capture"}[i],
                62 + (i / 2) * 354, 484 + (i % 2) * 62, 280, 24, juce::Justification::centredLeft);
    }
    g.setColour(rule); g.drawHorizontalLine(669, 32, getWidth() - 32.0f);
    g.setColour(ink); g.setFont(serif(18));
    g.drawText("Pitch randomization", 42, 679, 300, 24, juce::Justification::centredLeft);
    float total = 0;
    for (int i = 0; i < 4; ++i) total += processor.value(GranularProcessor::moduleID(selectedModule, "chance" + juce::String(i)));
    g.setFont(sans(12)); g.setColour(muted);
    g.drawText("Original pitch: " + juce::String(juce::jmax(0.0f, 100.0f - total), 1) + "%", 600, 679, 338, 24, juce::Justification::centredRight);
    for (int i = 0; i < 4; ++i)
    {
        const int x = 42 + i * width;
        g.drawText("Choice " + juce::String(i + 1) + " / semitones", x, 706, width - 20, 18, juce::Justification::centredLeft);
        const float chance = processor.value(GranularProcessor::moduleID(selectedModule, "chance" + juce::String(i)))
            * 100.0f / juce::jmax(100.0f, total);
        g.drawText("Chance: " + juce::String(chance, 1) + "%", x, 760, width - 20, 18, juce::Justification::centredLeft);
    }
    g.drawText("Unused chance plays the original pitch. Totals above 100% are scaled proportionally.", 42, 819, 890, 20, juce::Justification::centredLeft);
    g.setColour(rule); g.drawHorizontalLine(846, 32, getWidth() - 32.0f);
    g.setColour(ink); g.setFont(serif(18));
    g.drawText("Modulation", 42, 852, 135, 28, juce::Justification::centredLeft);
    g.setFont(sans(11)); g.setColour(muted);
    g.drawText("Waveform", 42, 884, 155, 18, juce::Justification::centredLeft);
    g.drawText("Rate", 210, 884, 180, 18, juce::Justification::centredLeft);
    g.drawText("Timing", 405, 884, 110, 18, juce::Justification::centredLeft);
    g.drawText(patchMessage.isEmpty() ? "Drag an output to a control input. Hover an output to see its cables." : patchMessage,
        535, 892, getWidth() - 577, 38, juce::Justification::centredLeft);
    g.drawText("Connections (" + juce::String(shownConnections) + ") / scroll to view", 42, 936, 300, 18, juce::Justification::centredLeft);
    g.drawText("Minimum", 362, 936, 275, 18, juce::Justification::centredLeft);
    g.drawText("Maximum", 627, 936, 275, 18, juce::Justification::centredLeft);
    for (int slot = 0; slot < shownModCount; ++slot)
        g.drawText("LFO " + juce::String(slot + 1), 663 + slot * 90, 855, 45, 22, juce::Justification::centredRight);

}

void GranularEditor::refreshModulation()
{
    waveAttachment.reset(); syncAttachment.reset(); rateAttachment.reset();
    for (auto& row : connectionRows) { row.moduleAttachment.reset(); row.targetAttachment.reset(); row.minAttachment.reset(); row.maxAttachment.reset(); }
    shownModCount = juce::jlimit(0, GranularProcessor::maxModulators,
        juce::roundToInt(processor.value("modCount")));
    selectedSource = juce::jlimit(0, juce::jmax(0, shownModCount - 1), selectedSource);
    sourceSelector.clear(juce::dontSendNotification);
    for (int i = 0; i < shownModCount; ++i) sourceSelector.addItem("LFO " + juce::String(i + 1), i + 1);
    sourceSelector.setSelectedId(shownModCount > 0 ? selectedSource + 1 : 0, juce::dontSendNotification);
    sourceSelector.setTextWhenNothingSelected("No modulators");
    const bool active = shownModCount > 0;
    sourceSelector.setEnabled(active); modWave.setEnabled(active); modSync.setEnabled(active); modRate.setEnabled(active);
    modRate.setColour(juce::Slider::trackColourId, bandColours[selectedModule]);
    addModulator.setEnabled(shownModCount < GranularProcessor::maxModulators); removeModulator.setEnabled(active);
    for (int i = 0; i < GranularProcessor::maxModulators; ++i) outputSockets[i]->setVisible(i < shownModCount);
    if (hoveredSource >= shownModCount) hoveredSource = -1;
    shownConnections = active ? juce::jlimit(0, GranularProcessor::maxConnections,
        juce::roundToInt(processor.value(GranularProcessor::modID(selectedSource, "connections")))) : 0;
    if (active)
    {
        waveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.parameters,
            GranularProcessor::modID(selectedSource, "wave"), modWave);
        syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.parameters,
            GranularProcessor::modID(selectedSource, "sync"), modSync);
        rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters,
            GranularProcessor::modID(selectedSource, "rate"), modRate);
    }
    for (int link = 0; link < GranularProcessor::maxConnections; ++link)
    {
        auto& row = connectionRows[link];
        row.minimum.setColour(juce::Slider::trackColourId, bandColours[selectedModule]);
        row.maximum.setColour(juce::Slider::trackColourId, bandColours[selectedModule]);
        const bool visible = link < shownConnections;
        row.module.setVisible(visible); row.target.setVisible(visible); row.minimum.setVisible(visible); row.maximum.setVisible(visible); row.remove.setVisible(visible);
        if (!visible) continue;
        row.moduleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.parameters,
            GranularProcessor::connectionID(selectedSource, link, "module"), row.module);
        row.targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.parameters,
            GranularProcessor::connectionID(selectedSource, link, "target"), row.target);
        row.minAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters,
            GranularProcessor::connectionID(selectedSource, link, "min"), row.minimum);
        row.maxAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.parameters,
            GranularProcessor::connectionID(selectedSource, link, "max"), row.maximum);
        updateRangeText(link);
    }
    if (getWidth() > 0) resized();
    repaint();
}
void GranularEditor::updateRangeText(int link)
{
    auto& row = connectionRows[link];
    const int target = juce::jlimit(0, GranularProcessor::modTargetCount - 1, row.target.getSelectedItemIndex());
    const int module = juce::jlimit(0, maxBands - 1, row.module.getSelectedItemIndex());
    auto* parameter = processor.parameters.getParameter(GranularProcessor::moduleID(module, GranularProcessor::modTargets[target]));
    for (auto* slider : {&row.minimum, &row.maximum})
    {
        slider->textFromValueFunction = [parameter](double value) { return parameter->getText(static_cast<float>(value * 0.01), 30) + " " + parameter->getLabel(); };
        slider->valueFromTextFunction = [parameter](const juce::String& value) { return parameter->getValueForText(value) * 100.0; };
        slider->updateText();
    }
}
void GranularEditor::connectModulator(int source, int target)
{
    const auto countID = GranularProcessor::modID(source, "connections");
    const int count = juce::roundToInt(processor.value(countID));
    for (int link = 0; link < count; ++link)
        if (juce::roundToInt(processor.value(GranularProcessor::connectionID(source, link, "module"))) == selectedModule
            && juce::roundToInt(processor.value(GranularProcessor::connectionID(source, link, "target"))) == target)
        { selectedSource = source; refreshModulation(); return; }
    if (count == GranularProcessor::maxConnections)
    { patchMessage = "This output has four connections. Remove one to add another."; repaint(); return; }
    processor.setValue(GranularProcessor::connectionID(source, count, "module"), static_cast<float>(selectedModule));
    processor.setValue(GranularProcessor::connectionID(source, count, "target"), static_cast<float>(target));
    processor.setValue(GranularProcessor::connectionID(source, count, "min"), 0);
    processor.setValue(GranularProcessor::connectionID(source, count, "max"), 100);
    processor.setValue(countID, static_cast<float>(count + 1));
    selectedSource = source;
    patchMessage = "Set this connection's minimum and maximum below.";
    refreshModulation();
    connectionViewport.setViewPosition(0, count * 38);
}
void GranularEditor::removeConnection(int connection)
{
    const auto countID = GranularProcessor::modID(selectedSource, "connections");
    const int count = juce::roundToInt(processor.value(countID));
    if (connection >= count) return;
    for (int link = connection; link < count - 1; ++link)
        for (const auto* id : {"module", "target", "min", "max"})
            processor.setValue(GranularProcessor::connectionID(selectedSource, link, id),
                processor.value(GranularProcessor::connectionID(selectedSource, link + 1, id)));
    processor.setValue(countID, static_cast<float>(count - 1));
    refreshModulation();
}
void GranularEditor::showPlayback(bool playback)
{
    playbackVisible = playback;
    grainsPage.setToggleState(!playback, juce::dontSendNotification);
    playbackPage.setToggleState(playback, juce::dontSendNotification);
    for (auto& slider : sliders) slider.setVisible(!playback);
    for (auto& slider : feedbackSliders) slider.setVisible(!playback);
    envelope->setVisible(!playback);
    for (auto& slider : playbackSliders) slider.setVisible(playback);
    freezeButton.setVisible(playback);
    for (int i = 0; i < 8; ++i) inputSockets[i]->setVisible(!playback);
    for (int i = 16; i < GranularProcessor::modTargetCount; ++i) inputSockets[i]->setVisible(playback);
    hoveredTarget = -1;
    repaint();
}
void GranularEditor::paintOverChildren(juce::Graphics& g)
{
    for (int module = 0; module < maxBands; ++module)
    {
        g.setColour(module == selectedModule ? paper : muted);
        g.drawEllipse(static_cast<float>(tabs[module].getRight() - 12), 414.5f, 7, 7, 1.2f);
    }
    const int selectedOutput = draggingSource >= 0 ? draggingSource : hoveredSource;
    if (selectedOutput < 0 && hoveredTarget < 0) return;
    auto drawWire = [&](juce::Point<float> start, juce::Point<float> end, int colour)
    {
        juce::Path path; path.startNewSubPath(start);
        path.cubicTo(start.translated(0, -90), end.translated(0, 90), end);
        g.setColour(bandColours[colour].withAlpha(0.85f));
        g.strokePath(path, juce::PathStrokeType(2));
    };
    for (int source = 0; source < shownModCount; ++source)
    {
        if (selectedOutput >= 0 && selectedOutput != source) continue;
        const auto start = outputSockets[source]->getBounds().getCentre().toFloat();
        const int count = juce::jlimit(0, GranularProcessor::maxConnections,
            juce::roundToInt(processor.value(GranularProcessor::modID(source, "connections"))));
        for (int link = 0; link < count; ++link)
        {
            const int target = juce::jlimit(0, GranularProcessor::modTargetCount - 1,
                juce::roundToInt(processor.value(GranularProcessor::connectionID(source, link, "target"))));
            const int module = juce::jlimit(0, maxBands - 1,
                juce::roundToInt(processor.value(GranularProcessor::connectionID(source, link, "module"))));
            if (selectedOutput < 0 && (module != selectedModule || target != hoveredTarget)) continue;
            if (module == selectedModule)
            {
                const auto end = inputSockets[target]->isVisible() ? inputSockets[target]->getBounds().getCentre().toFloat()
                    : (target >= 16 ? playbackPage : grainsPage).getBounds().getCentre().toFloat();
                drawWire(start, end, module);
            }
            else
            {
                drawWire(start, {static_cast<float>(tabs[module].getRight() - 8), 418}, module);
                g.setColour(bandColours[module]); g.drawRect(tabs[module].getBounds().toFloat().reduced(1), 2);
            }
        }
    }
    if (draggingSource >= 0 && draggingSource < shownModCount)
        drawWire(outputSockets[draggingSource]->getBounds().getCentre().toFloat(), wireEnd, selectedModule);
}

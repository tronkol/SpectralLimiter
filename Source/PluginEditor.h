#pragma once
#include "PluginProcessor.h"

namespace ui
{
    const juce::Colour bg      { 0xff17191d };
    const juce::Colour panel   { 0xff1f2228 };
    const juce::Colour grid    { 0xff2c3038 };
    const juce::Colour text    { 0xffd9dde3 };
    const juce::Colour dim     { 0xff7d8591 };
    const juce::Colour accent  { 0xffff8a2a }; // riduzione
    const juce::Colour blue    { 0xff4fb0ff }; // ingresso / uscita
}

class LimiterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LimiterLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (13.0f)); }
};

class SpectrumDisplay : public juce::Component
{
public:
    explicit SpectrumDisplay (const sl::Engine*& engineRef) : eng (engineRef) { in.fill (-120.0f); out.fill (-120.0f); }
    void paint (juce::Graphics&) override;
    void setThresholdDb (float t) { thr = t; }
    void update();
private:
    const sl::Engine*& eng;
    float thr = -18.0f;
    std::array<float, sl::Engine::kDisplayPoints> in {}, out {}, gr {};
    float fs = 48000.0f;
public:
    void setSampleRate (float f) { fs = f; }
};

class MeterBar : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void setValues (float outDb, float grDb) { out = outDb; gr = grDb; repaint(); }
private:
    float out = -120.0f, gr = 0.0f, hold = -120.0f;
};

class SpectralLimiterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpectralLimiterEditor (SpectralLimiterProcessor&);
    ~SpectralLimiterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    SpectralLimiterProcessor& proc;
    LimiterLookAndFeel laf;
    const sl::Engine* engPtr = nullptr;

    SpectrumDisplay display;
    MeterBar meter;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label name, value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    Knob kThr, kRatio, kGain, kBal, kAtk, kRel, kCeil;
    juce::ComboBox bands;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bandsAtt;
    juce::ComboBox oversampling;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAtt;
    juce::Label bandsLabel, latencyLabel;

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& label);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralLimiterEditor)
};

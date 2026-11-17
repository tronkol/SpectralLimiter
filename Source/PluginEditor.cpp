#include "PluginEditor.h"

//==============================================================================
LimiterLookAndFeel::LimiterLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, ui::panel);
    setColour (juce::ComboBox::textColourId, ui::text);
    setColour (juce::ComboBox::outlineColourId, ui::grid);
    setColour (juce::ComboBox::arrowColourId, ui::dim);
    setColour (juce::PopupMenu::backgroundColourId, ui::panel);
    setColour (juce::PopupMenu::textColourId, ui::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::grid);
    setColour (juce::Label::textColourId, ui::text);
}

void LimiterLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float arcR = r - 2.0f;
    const float ang = a0 + pos * (a1 - a0);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, a1, true);
    g.setColour (ui::grid);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // arco valore: parte dal centro se la scala e' bipolare (balance), altrimenti dal minimo
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0 && s.getMinimum() == -s.getMaximum();
    const float aStart = bipolar ? (a0 + 0.5f * (a1 - a0)) : a0;
    juce::Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, std::min (aStart, ang), std::max (aStart, ang), true);
    g.setColour (s.isEnabled() ? ui::accent : ui::dim);
    g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float br = arcR - 8.0f;
    g.setColour (ui::panel);
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (ui::grid);
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);

    juce::Path ptr;
    ptr.addRoundedRectangle (-1.5f, -br + 3.0f, 3.0f, br * 0.45f, 1.5f);
    g.setColour (ui::text);
    g.fillPath (ptr, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void LimiterLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (ui::panel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (ui::grid);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    juce::Path p;
    const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
    p.startNewSubPath (cx - 4.0f, cy - 2.0f);
    p.lineTo (cx, cy + 2.0f);
    p.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (ui::dim);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

//==============================================================================
static float freqToX (float f, float fMin, float fMax, float w)
{
    return w * std::log (f / fMin) / std::log (fMax / fMin);
}

void SpectrumDisplay::update()
{
    if (eng == nullptr) return;
    for (int i = 0; i < sl::Engine::kDisplayPoints; ++i)
    {
        const float a = eng->dispIn[i].load(), b = eng->dispOut[i].load(), c = eng->dispGR[i].load();
        // smorzamento visivo: salita rapida, discesa lenta
        in[(size_t) i]  = a > in[(size_t) i]  ? a : in[(size_t) i]  + (a - in[(size_t) i])  * 0.25f;
        out[(size_t) i] = b > out[(size_t) i] ? b : out[(size_t) i] + (b - out[(size_t) i]) * 0.25f;
        gr[(size_t) i]  = c < gr[(size_t) i]  ? c : gr[(size_t) i]  + (c - gr[(size_t) i])  * 0.25f;
    }
    repaint();
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (ui::panel);
    g.fillRoundedRectangle (b, 8.0f);

    const auto area = b.reduced (10.0f, 8.0f).withTrimmedBottom (14.0f).withTrimmedLeft (26.0f);
    const float dbTop = 0.0f, dbBot = -90.0f;
    auto yOf = [&] (float db) { return area.getY() + area.getHeight() * (dbTop - juce::jlimit (dbBot, dbTop, db)) / (dbTop - dbBot); };

    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (int db = 0; db >= -90; db -= 15)
    {
        g.setColour (ui::grid);
        g.drawHorizontalLine ((int) yOf ((float) db), area.getX(), area.getRight());
        g.setColour (ui::dim);
        g.drawText (juce::String (db), (int) b.getX() + 4, (int) yOf ((float) db) - 6, 22, 12, juce::Justification::centredRight);
    }
    const float fMin = 20.0f, fMax = std::min (20000.0f, 0.499f * fs);
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        if (f > fMax) continue;
        const float x = area.getX() + freqToX (f, fMin, fMax, area.getWidth());
        g.setColour (ui::grid);
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.setColour (ui::dim);
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                    (int) x - 16, (int) area.getBottom() + 1, 32, 12, juce::Justification::centred);
    }

    const int np = sl::Engine::kDisplayPoints;
    auto xOf = [&] (int i) { return area.getX() + area.getWidth() * ((float) i + 0.5f) / (float) np; };

    // riduzione per banda: appesa dall'alto, arancione
    {
        juce::Path p;
        p.startNewSubPath (area.getX(), area.getY());
        for (int i = 0; i < np; ++i)
            p.lineTo (xOf (i), area.getY() + area.getHeight() * juce::jlimit (0.0f, 1.0f, -gr[(size_t) i] / 36.0f) * 0.45f);
        p.lineTo (area.getRight(), area.getY());
        p.closeSubPath();
        g.setColour (ui::accent.withAlpha (0.35f));
        g.fillPath (p);
        g.setColour (ui::accent.withAlpha (0.9f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    auto curve = [&] (const std::array<float, sl::Engine::kDisplayPoints>& v, juce::Colour col, float alphaFill)
    {
        juce::Path p;
        p.startNewSubPath (xOf (0), yOf (v[0]));
        for (int i = 1; i < np; ++i) p.lineTo (xOf (i), yOf (v[(size_t) i]));
        if (alphaFill > 0.0f)
        {
            juce::Path f (p);
            f.lineTo (xOf (np - 1), area.getBottom());
            f.lineTo (xOf (0), area.getBottom());
            f.closeSubPath();
            g.setColour (col.withAlpha (alphaFill));
            g.fillPath (f);
        }
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
    };
    curve (in, ui::dim, 0.0f);
    curve (out, ui::blue, 0.18f);

    // soglia
    g.setColour (ui::text.withAlpha (0.8f));
    const float ty = yOf (thr);
    const float dash[] = { 5.0f, 4.0f };
    g.drawDashedLine (juce::Line<float> (area.getX(), ty, area.getRight(), ty), dash, 2, 1.0f);
}

//==============================================================================
void MeterBar::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (ui::panel);
    g.fillRoundedRectangle (b, 8.0f);
    const auto inner = b.reduced (8.0f, 10.0f);
    const float half = inner.getWidth() * 0.5f - 4.0f;
    auto yOf = [&] (float db, float lo, float hi) { return inner.getBottom() - inner.getHeight() * juce::jlimit (0.0f, 1.0f, (db - lo) / (hi - lo)); };

    // uscita (blu), -48..0
    {
        const auto r = juce::Rectangle<float> (inner.getX(), inner.getY(), half, inner.getHeight());
        g.setColour (ui::grid);
        g.fillRoundedRectangle (r, 3.0f);
        const float y = yOf (out, -48.0f, 0.0f);
        g.setColour (out > -0.5f ? ui::accent : ui::blue);
        g.fillRoundedRectangle (r.withTop (y), 3.0f);
    }
    // riduzione brickwall (arancione), 0..-12 dall'alto
    {
        const auto r = juce::Rectangle<float> (inner.getRight() - half, inner.getY(), half, inner.getHeight());
        g.setColour (ui::grid);
        g.fillRoundedRectangle (r, 3.0f);
        const float h = inner.getHeight() * juce::jlimit (0.0f, 1.0f, -gr / 12.0f);
        g.setColour (ui::accent);
        g.fillRoundedRectangle (r.withHeight (h), 3.0f);
    }
}

//==============================================================================
void SpectralLimiterEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& label)
{
    addAndMakeVisible (k.slider);
    k.slider.setLookAndFeel (&laf);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    k.name.setText (label, juce::dontSendNotification);
    k.name.setJustificationType (juce::Justification::centred);
    k.name.setColour (juce::Label::textColourId, ui::dim);
    k.name.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (k.name);
    k.value.setJustificationType (juce::Justification::centred);
    k.value.setColour (juce::Label::textColourId, ui::text);
    k.value.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (k.value);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);

    auto* param = proc.apvts.getParameter (id);
    auto refresh = [&k, param]
    {
        k.value.setText (param->getText (param->getValue(), 20), juce::dontSendNotification);
    };
    k.slider.onValueChange = refresh;
    refresh();
}

SpectralLimiterEditor::SpectralLimiterEditor (SpectralLimiterProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (engPtr)
{
    setLookAndFeel (&laf);
    setSize (780, 460);

    addAndMakeVisible (display);
    addAndMakeVisible (meter);
    setupKnob (kThr, "threshold", "THRESHOLD");
    setupKnob (kRatio, "ratio", "RATIO");
    setupKnob (kGain, "gain", "GAIN");
    setupKnob (kBal, "balance", "BALANCE");
    setupKnob (kAtk, "attack", "ATTACK");
    setupKnob (kRel, "release", "RELEASE");
    setupKnob (kCeil, "ceiling", "CEILING");

    bands.addItemList (juce::StringArray { "513 bande", "1025 bande", "2049 bande", "4097 bande", "8193 bande", "16385 bande", "32769 bande" }, 1);
    addAndMakeVisible (bands);
    bandsAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "bands", bands);

    oversampling.addItemList (juce::StringArray { "OS off", "OS 2x", "OS 4x" }, 1);
    addAndMakeVisible (oversampling);
    osAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "oversampling", oversampling);

    latencyLabel.setJustificationType (juce::Justification::centredRight);
    latencyLabel.setColour (juce::Label::textColourId, ui::dim);
    latencyLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (latencyLabel);

    if (auto* o = proc.currentEngine()) engPtr = &o->core();
    startTimerHz (30);
    timerCallback();
}

SpectralLimiterEditor::~SpectralLimiterEditor()
{
    stopTimer();
    for (auto* k : { &kThr, &kRatio, &kGain, &kBal, &kAtk, &kRel, &kCeil }) k->slider.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void SpectralLimiterEditor::timerCallback()
{
    const auto* osp = proc.currentEngine();
    if (osp == nullptr) return;
    const auto& os = *osp;
    engPtr = &os.core();
    display.setSampleRate ((float) os.baseSampleRate());
    display.setThresholdDb (kThr.slider.getValue() > -100 ? (float) kThr.slider.getValue() : -18.0f);
    display.update();
    meter.setValues (engPtr->meterOutDb.load(), engPtr->meterGRdb.load());

    const int lat = os.latencySamples();
    const double fs = os.baseSampleRate();
    latencyLabel.setText ("latenza " + juce::String (fs > 0 ? 1000.0 * lat / fs : 0.0, 1) + " ms", juce::dontSendNotification);
}

void SpectralLimiterEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    g.setColour (ui::text);
    g.setFont (juce::Font (juce::FontOptions (17.0f)));
    g.drawText ("LIMITER SPETTRALE", 20, 12, 300, 24, juce::Justification::centredLeft);
}

void SpectralLimiterEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto head = r.removeFromTop (30);
    bands.setBounds (head.removeFromRight (130).reduced (0, 2));
    head.removeFromRight (8);
    oversampling.setBounds (head.removeFromRight (84).reduced (0, 2));
    head.removeFromRight (8);
    latencyLabel.setBounds (head.removeFromRight (160));
    r.removeFromTop (8);

    auto knobs = r.removeFromBottom (130);
    r.removeFromBottom (10);
    meter.setBounds (r.removeFromRight (64));
    r.removeFromRight (10);
    display.setBounds (r);

    const int w = knobs.getWidth() / 7;
    int i = 0;
    for (auto* k : { &kThr, &kRatio, &kGain, &kBal, &kAtk, &kRel, &kCeil })
    {
        auto c = knobs.withX (knobs.getX() + w * i++).withWidth (w).reduced (6, 0);
        k->name.setBounds (c.removeFromTop (16));
        k->value.setBounds (c.removeFromBottom (18));
        k->slider.setBounds (c);
    }
}

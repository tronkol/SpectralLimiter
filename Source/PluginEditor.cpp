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

    drawCurve (g);
}

//==============================================================================
// Curva di focus: punti trascinabili. x = frequenza, y = guadagno (shelf/peak); rotella = Q.
juce::Rectangle<float> SpectrumDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedBottom (14.0f).withTrimmedLeft (26.0f);
}

float SpectrumDisplay::xOfFreq (float f) const
{
    const auto a = plotArea();
    return a.getX() + freqToX (juce::jlimit (20.0f, fMax(), f), 20.0f, fMax(), a.getWidth());
}

float SpectrumDisplay::freqOfX (float x) const
{
    const auto a = plotArea();
    const float t = juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth());
    return 20.0f * std::pow (fMax() / 20.0f, t);
}

float SpectrumDisplay::getP (const char* key, int i) const
{
    return apvts.getRawParameterValue (SpectralLimiterProcessor::curveId (key, i))->load();
}

void SpectrumDisplay::setP (const char* key, int i, float v)
{
    if (auto* p = apvts.getParameter (SpectralLimiterProcessor::curveId (key, i)))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void SpectrumDisplay::gesture (int i, bool begin)
{
    for (const char* k : { "type", "freq", "gain", "q" })
        if (auto* p = apvts.getParameter (SpectralLimiterProcessor::curveId (k, i)))
        {
            if (begin) p->beginChangeGesture(); else p->endChangeGesture();
        }
}

sl::CurvePoint SpectrumDisplay::readPoint (int i) const
{
    sl::CurvePoint c;
    c.type = juce::jlimit (0, 5, (int) std::lround (getP ("type", i)));
    c.freq = getP ("freq", i);
    c.gainDb = getP ("gain", i);
    c.q = getP ("q", i);
    return c;
}

juce::Point<float> SpectrumDisplay::pointPos (int i) const
{
    const auto c = readPoint (i);
    const float db = (c.type == sl::kLowCut || c.type == sl::kHighCut) ? 0.0f : (float) c.gainDb; // i cut si spostano solo in frequenza
    return { xOfFreq ((float) c.freq), yOfCurve (db) };
}

int SpectrumDisplay::hitTest (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 12.0f;
    for (int i = 0; i < sl::kMaxCurvePoints; ++i)
    {
        if (readPoint (i).type == sl::kOff) continue;
        const float d = pointPos (i).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void SpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h != hoverIdx) { hoverIdx = h; repaint(); }
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h < 0) return;
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Low cut");
        m.addItem (2, "Low shelf");
        m.addItem (3, "Peak");
        m.addItem (4, "High shelf");
        m.addItem (5, "High cut");
        m.addSeparator();
        m.addItem (6, "Elimina punto");
        juce::Component::SafePointer<SpectrumDisplay> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe, h] (int r)
        {
            if (safe == nullptr || r == 0) return;
            safe->gesture (h, true);
            safe->setP ("type", h, r == 6 ? 0.0f : (float) r);
            safe->gesture (h, false);
            safe->repaint();
        });
        return;
    }
    dragIdx = h;
    gesture (h, true);
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIdx < 0) return;
    setP ("freq", dragIdx, freqOfX (e.position.x));
    const int t = (int) std::lround (getP ("type", dragIdx));
    if (t == sl::kLowShelf || t == sl::kPeak || t == sl::kHighShelf)
        setP ("gain", dragIdx, curveOfY (e.position.y));
    repaint();
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragIdx >= 0) gesture (dragIdx, false);
    dragIdx = -1;
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h >= 0)
    {
        gesture (h, true);
        setP ("type", h, 0.0f);
        gesture (h, false);
        hoverIdx = -1;
        repaint();
        return;
    }
    for (int i = 0; i < sl::kMaxCurvePoints; ++i)
        if (readPoint (i).type == sl::kOff)
        {
            gesture (i, true);
            setP ("type", i, (float) sl::kPeak);
            setP ("freq", i, freqOfX (e.position.x));
            setP ("gain", i, curveOfY (e.position.y));
            setP ("q", i, 1.0f);
            gesture (i, false);
            repaint();
            return;
        }
}

void SpectrumDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int h = hitTest (e.position);
    if (h < 0) return;
    const float q = juce::jlimit (0.3f, 8.0f, getP ("q", h) * std::exp (w.deltaY * 0.8f));
    gesture (h, true);
    setP ("q", h, q);
    gesture (h, false);
    repaint();
}

void SpectrumDisplay::drawCurve (juce::Graphics& g)
{
    const auto area = plotArea();
    sl::CurvePoint pts[sl::kMaxCurvePoints];
    int active = 0;
    for (int i = 0; i < sl::kMaxCurvePoints; ++i)
    {
        pts[i] = readPoint (i);
        if (pts[i].type != sl::kOff) ++active;
    }
    const juce::Colour curveCol (0xff62e0a8);

    // riga dello 0 dB (intervento pieno)
    g.setColour (curveCol.withAlpha (0.25f));
    g.drawHorizontalLine ((int) yOfCurve (0.0f), area.getX(), area.getRight());

    juce::Path path;
    const int n = 240;
    for (int i = 0; i <= n; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) n;
        const double c = std::max (-24.0, std::min (6.0, sl::curveTotalDb (pts, sl::kMaxCurvePoints, (double) freqOfX (x))));
        const float y = yOfCurve ((float) c);
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    g.setColour (curveCol.withAlpha (active > 0 ? 0.95f : 0.45f));
    g.strokePath (path, juce::PathStrokeType (1.6f));

    for (int i = 0; i < sl::kMaxCurvePoints; ++i)
    {
        if (pts[i].type == sl::kOff) continue;
        const auto pos = pointPos (i);
        const float r = (i == hoverIdx || i == dragIdx) ? 6.5f : 5.0f;
        g.setColour (ui::bg);
        g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
        g.setColour (curveCol);
        g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, 1.8f);
    }

    if (hoverIdx >= 0 && pts[hoverIdx].type != sl::kOff)
    {
        static const char* names[] = { "", "Low cut", "Low shelf", "Peak", "High shelf", "High cut" };
        const auto& c = pts[hoverIdx];
        juce::String f = c.freq >= 1000.0 ? juce::String (c.freq / 1000.0, 2) + " kHz" : juce::String ((int) c.freq) + " Hz";
        juce::String txt = juce::String (names[c.type]) + "  " + f;
        if (c.type == sl::kLowShelf || c.type == sl::kPeak || c.type == sl::kHighShelf)
            txt << "  " << juce::String (c.gainDb, 1) << " dB";
        txt << "  Q " << juce::String (c.q, 2);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (ui::text);
        g.drawText (txt, (int) area.getX() + 6, (int) area.getY() + 2, 300, 14, juce::Justification::centredLeft);
    }
    else if (active == 0)
    {
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (ui::dim);
        g.drawText ("Focus: doppio click per aggiungere un punto, tasto destro per il tipo, rotella per la Q",
                    (int) area.getX() + 6, (int) area.getY() + 2, (int) area.getWidth() - 12, 14, juce::Justification::centredLeft);
    }
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
    : AudioProcessorEditor (&p), proc (p), display (engPtr, p.apvts)
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

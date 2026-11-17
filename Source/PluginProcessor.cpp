#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout SpectralLimiterProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    auto dbText = [] (float v, int) { return String (v, 1) + " dB"; };
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "threshold", 1 }, "Threshold",
                                                   NormalisableRange<float> (-48.0f, 0.0f, 0.1f), -18.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain",
                                                   NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "balance", 1 }, "Balance",
                                                   NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (
                                                       [] (float v, int) { return v == 0.0f ? String ("C") : (v < 0 ? "L " : "R ") + String ((int) std::abs (v)) + "%"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ceiling", 1 }, "Ceiling",
                                                   NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -1.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release",
                                                   NormalisableRange<float> (10.0f, 1000.0f, 1.0f, 0.4f), 150.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (
                                                       [] (float v, int) { return String ((int) v) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ratio", 1 }, "Ratio",
                                                   NormalisableRange<float> (1.0f, 100.0f, 0.1f, 0.35f), 100.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (
                                                       [] (float v, int) { return v >= 99.9f ? String ("inf:1") : String (v, 1) + ":1"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack",
                                                   NormalisableRange<float> (0.0f, 500.0f, 1.0f, 0.4f), 0.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (
                                                       [] (float v, int) { return v < 1.0f ? String ("istantaneo") : String ((int) v) + " ms"; })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "oversampling", 1 }, "Oversampling",
                                                    StringArray { "Off", "2x", "4x" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "bands", 1 }, "Bands",
                                                    StringArray { "513", "1025", "2049", "4097", "8193", "16385", "32769" }, 2));
    return l;
}

SpectralLimiterProcessor::SpectralLimiterProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& sp : slots) sp.store (nullptr);
    pThr = apvts.getRawParameterValue ("threshold");
    pGain = apvts.getRawParameterValue ("gain");
    pBal = apvts.getRawParameterValue ("balance");
    pCeil = apvts.getRawParameterValue ("ceiling");
    pRel = apvts.getRawParameterValue ("release");
    pAtk = apvts.getRawParameterValue ("attack");
    pRatio = apvts.getRawParameterValue ("ratio");
    pOs = apvts.getRawParameterValue ("oversampling");
    pBands = apvts.getRawParameterValue ("bands");
}

bool SpectralLimiterProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SpectralLimiterProcessor::ensureSlot (int slot)
{
    if (slots[(size_t) slot].load() != nullptr) return;
    auto e = std::make_unique<sl::OsEngine>();
    e->prepare (currentFs, fftSizeForIndex (slot % kNumSizes), osFactor (slot / kNumSizes), currentBlock);
    sl::OsEngine* raw = e.get();
    owned[(size_t) slot] = std::move (e);
    slots[(size_t) slot].store (raw, std::memory_order_release);
}

void SpectralLimiterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    suspendProcessing (true);
    for (auto& sp : slots) sp.store (nullptr);
    for (auto& o : owned) o.reset();
    currentFs = sampleRate;
    currentBlock = juce::jlimit (64, 8192, samplesPerBlock);
    const int size = juce::jlimit (0, kNumSizes - 1, (int) std::lround (pBands->load()));
    const int os = juce::jlimit (0, kNumOs - 1, (int) std::lround (pOs->load()));
    lastSlot = slotIndex (size, os);
    ensureSlot (lastSlot);
    activeSlot.store (lastSlot);
    slots[(size_t) lastSlot].load()->reset();
    setLatencySamples (slots[(size_t) lastSlot].load()->latencySamples());
    suspendProcessing (false);
}

void SpectralLimiterProcessor::handleAsyncUpdate()
{
    const int req = requestedSlot.load();
    if (req >= 0)
    {
        ensureSlot (req); // crea (se serve) sul thread messaggi; il thread audio lo vedra' pronto
        requestedSlot.store (-1);
    }
    if (auto* e = slots[(size_t) activeSlot.load()].load())
        setLatencySamples (e->latencySamples());
}

void SpectralLimiterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || currentFs <= 0.0)
        return;

    const int size = juce::jlimit (0, kNumSizes - 1, (int) std::lround (pBands->load()));
    const int os = juce::jlimit (0, kNumOs - 1, (int) std::lround (pOs->load()));
    const int want = slotIndex (size, os);
    if (want != lastSlot)
    {
        if (auto* ne = slots[(size_t) want].load (std::memory_order_acquire))
        {
            lastSlot = want;
            ne->reset();
            activeSlot.store (want);
            triggerAsyncUpdate(); // aggiorna la latenza dichiarata
        }
        else if (requestedSlot.load() != want)
        {
            requestedSlot.store (want); // prepara l'engine in background, intanto si continua con quello attuale
            triggerAsyncUpdate();
        }
    }

    sl::OsEngine* e = slots[(size_t) activeSlot.load()].load (std::memory_order_acquire);
    if (e == nullptr) return;
    sl::Engine::Params p;
    p.thresholdDb = pThr->load();
    p.gainDb = pGain->load();
    p.balance = pBal->load() / 100.0;
    p.ceilingDb = pCeil->load();
    p.releaseMs = pRel->load();
    p.attackMs = pAtk->load();
    p.ratio = pRatio->load();
    e->setParams (p);
    e->process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
}

juce::AudioProcessorEditor* SpectralLimiterProcessor::createEditor() { return new SpectralLimiterEditor (*this); }

void SpectralLimiterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SpectralLimiterProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpectralLimiterProcessor(); }

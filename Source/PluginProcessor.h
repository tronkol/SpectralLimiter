#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include "Oversampler.h"

class SpectralLimiterProcessor : public juce::AudioProcessor,
                                 private juce::AsyncUpdater
{
public:
    static constexpr int kNumSizes = 7; // 1024 ... 65536  ->  513 ... 32769 bande
    static int fftSizeForIndex (int i) { return 1024 << juce::jlimit (0, kNumSizes - 1, i); }

    SpectralLimiterProcessor();
    ~SpectralLimiterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Limiter Spettrale"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Per l'interfaccia (thread UI)
    const sl::OsEngine* currentEngine() const { return slots[(size_t) activeSlot.load()].load(std::memory_order_acquire); }
    juce::AudioProcessorValueTreeState apvts;

private:
    void handleAsyncUpdate() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int kNumOs = 3; // off, 2x, 4x
    static int osFactor (int i) { return i == 0 ? 1 : (i == 1 ? 2 : 4); }
    static constexpr int kNumSlots = kNumSizes * kNumOs;
    static int slotIndex (int size, int os) { return os * kNumSizes + size; }

    // Gli engine si creano nel thread messaggi (alloca molta memoria) e si pubblicano via puntatore atomico.
    std::array<std::unique_ptr<sl::OsEngine>, kNumSlots> owned;
    std::array<std::atomic<sl::OsEngine*>, kNumSlots> slots;
    std::atomic<int> activeSlot { 2 };
    std::atomic<int> requestedSlot { -1 };
    int lastSlot = 2;
    double currentFs = 0.0;
    int currentBlock = 4096;
    void ensureSlot (int slot);

    std::atomic<float>* pThr; std::atomic<float>* pGain; std::atomic<float>* pBal;
    std::atomic<float>* pCeil; std::atomic<float>* pRel; std::atomic<float>* pAtk; std::atomic<float>* pRatio; std::atomic<float>* pOs; std::atomic<float>* pBands;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralLimiterProcessor)
};

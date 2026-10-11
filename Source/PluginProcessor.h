#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include <vector>

#include "DeReverb.h"
#include "NoiseReducer.h"
#include "VoiceShifter.h"

class ClearVoiceProcessor : public juce::AudioProcessor
{
public:
    ClearVoiceProcessor();
    ~ClearVoiceProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ClearVoice"; }
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

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Voice knob range: -1 (deepest) .. 0 (calm, natural) .. +1 (highest)
    static constexpr float kMaxSemitones = 5.0f;

private:
    // A fixed-length delay used to keep dry signals time-aligned with processed ones.
    struct DelayLine
    {
        void prepare (int delaySamples)
        {
            buf.assign ((size_t) delaySamples + 1, 0.0f);
            w = 0;
        }
        float process (float x)
        {
            buf[(size_t) w] = x;
            w = (w + 1) % (int) buf.size();
            return buf[(size_t) w];
        }
        std::vector<float> buf { 0.0f };
        int w = 0;
    };

    struct Channel
    {
        NoiseReducer noise;
        DeReverb dereverb;
        VoiceShifter shifter;
        CalmStage calm;
        DelayLine noiseDry, voiceDry;
        std::vector<float> wet;
    };

    std::array<std::unique_ptr<Channel>, 2> channels;
    int numActiveChannels = 2;
    int chunkSize = 512;

    juce::SmoothedValue<float> noiseMix, reverbAmt, gain, voiceOnMix, shiftMix, pitchRatio;
    std::vector<float> noiseMixBuf, shiftMixBuf, voiceMixBuf, gainBuf;

    std::atomic<float>* pNoise = nullptr;
    std::atomic<float>* pReverb = nullptr;
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pVoiceOn = nullptr;
    std::atomic<float>* pVoice = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClearVoiceProcessor)
};

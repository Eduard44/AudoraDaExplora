#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ClearVoiceProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "noise", 1 }, "Noise", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "reverb", 1 }, "Reverb", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "volume", 1 }, "Volume", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "voiceOn", 1 }, "Voice On", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "voice", 1 }, "Voice", NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (std::abs (v) < 0.04f) return String ("Calm");
            return String (v < 0 ? "Deep " : "High ") + String (roundToInt (std::abs (v) * 100.0f)) + " %";
        })));

    return layout;
}

ClearVoiceProcessor::ClearVoiceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pNoise   = apvts.getRawParameterValue ("noise");
    pReverb  = apvts.getRawParameterValue ("reverb");
    pVolume  = apvts.getRawParameterValue ("volume");
    pVoiceOn = apvts.getRawParameterValue ("voiceOn");
    pVoice   = apvts.getRawParameterValue ("voice");
}

bool ClearVoiceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void ClearVoiceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    numActiveChannels = juce::jlimit (1, 2, getTotalNumInputChannels());
    chunkSize = std::max (64, samplesPerBlock);

    int noiseLatency = 0;
    for (int c = 0; c < 2; ++c)
    {
        if (c >= numActiveChannels) { channels[(size_t) c].reset(); continue; }
        auto ch = std::make_unique<Channel>();
        ch->noise.prepare (sampleRate, chunkSize);
        ch->dereverb.prepare (sampleRate);
        ch->shifter.prepare (sampleRate);
        ch->calm.prepare (sampleRate);
        noiseLatency = ch->noise.getLatency();
        ch->noiseDry.prepare (noiseLatency);
        ch->voiceDry.prepare (VoiceShifter::kLatency);
        ch->wet.assign ((size_t) chunkSize, 0.0f);
        channels[(size_t) c] = std::move (ch);
    }

    setLatencySamples (noiseLatency + DeReverb::kLatency + VoiceShifter::kLatency);

    auto setup = [sampleRate] (juce::SmoothedValue<float>& s, float v, double sec)
    {
        s.reset (sampleRate, sec);
        s.setCurrentAndTargetValue (v);
    };
    setup (noiseMix,   pNoise->load(), 0.03);
    setup (reverbAmt,  pReverb->load(), 0.05);
    setup (gain,       juce::Decibels::decibelsToGain (pVolume->load()), 0.03);
    setup (voiceOnMix, pVoiceOn->load() > 0.5f ? 1.0f : 0.0f, 0.03);
    setup (shiftMix,   0.0f, 0.03);
    setup (pitchRatio, 1.0f, 0.06);

    noiseMixBuf.assign ((size_t) chunkSize, 0.0f);
    shiftMixBuf.assign ((size_t) chunkSize, 0.0f);
    voiceMixBuf.assign ((size_t) chunkSize, 0.0f);
    gainBuf.assign ((size_t) chunkSize, 1.0f);
}

void ClearVoiceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = std::min (buffer.getNumChannels(), numActiveChannels);
    const int total = buffer.getNumSamples();

    for (int i = numCh; i < buffer.getNumChannels(); ++i)
        buffer.clear (i, 0, total);

    // Targets for this block
    noiseMix.setTargetValue (pNoise->load());
    reverbAmt.setTargetValue (pReverb->load());
    gain.setTargetValue (juce::Decibels::decibelsToGain (pVolume->load()));
    voiceOnMix.setTargetValue (pVoiceOn->load() > 0.5f ? 1.0f : 0.0f);

    float v = pVoice->load();
    if (std::abs (v) < 0.04f) v = 0.0f; // dead zone: the centre is exactly "no shift"
    const float ratioTarget = std::pow (2.0f, (v * kMaxSemitones) / 12.0f);
    pitchRatio.setTargetValue (ratioTarget);
    shiftMix.setTargetValue (v == 0.0f ? 0.0f : 1.0f);

    for (int start = 0; start < total; start += chunkSize)
    {
        const int n = std::min (chunkSize, total - start);

        for (int i = 0; i < n; ++i)
        {
            noiseMixBuf[(size_t) i] = noiseMix.getNextValue();
            shiftMixBuf[(size_t) i] = shiftMix.getNextValue();
            voiceMixBuf[(size_t) i] = voiceOnMix.getNextValue();
            gainBuf[(size_t) i]     = gain.getNextValue();
        }
        const float reverb = reverbAmt.skip (n);
        const float ratio  = pitchRatio.skip (n);

        for (int c = 0; c < numCh; ++c)
        {
            auto& ch = *channels[(size_t) c];
            float* data = buffer.getWritePointer (c) + start;

            ch.dereverb.setAmount (reverb);
            ch.shifter.setRatio (ratio);

            ch.noise.process (data, ch.wet.data(), n);

            for (int i = 0; i < n; ++i)
            {
                // 1. noise removal (dry kept time-aligned with the neural path)
                const float dry   = ch.noiseDry.process (data[i]);
                const float mixed = dry + (ch.wet[(size_t) i] - dry) * noiseMixBuf[(size_t) i];

                // 2. reverb removal
                const float y1 = ch.dereverb.processSample (mixed);

                // 3. voice section (always running so latency never changes)
                const float shifted = ch.shifter.processSample (y1);
                const float dryV    = ch.voiceDry.process (y1);
                const float base    = dryV + (shifted - dryV) * shiftMixBuf[(size_t) i];
                const float calm    = ch.calm.process (base);
                const float voiced  = dryV + (calm - dryV) * voiceMixBuf[(size_t) i];

                // 4. volume
                data[i] = voiced * gainBuf[(size_t) i];
            }
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* ClearVoiceProcessor::createEditor() { return new ClearVoiceEditor (*this); }

void ClearVoiceProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ClearVoiceProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ClearVoiceProcessor(); }

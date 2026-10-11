#include "PluginEditor.h"

namespace
{
    const juce::Colour bg      { 0xff12161b };
    const juce::Colour panel   { 0xff181d24 };
    const juce::Colour textDim { 0xff8a94a3 };
    const juce::Colour textHi  { 0xffe6eaf0 };

    void styleLabel (juce::Label& l, const juce::String& text, float size, juce::Colour colour,
                     juce::Justification j = juce::Justification::centred)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (size)));
        l.setColour (juce::Label::textColourId, colour);
        l.setJustificationType (j);
        l.setInterceptsMouseClicks (false, false);
    }
}

ClearVoiceEditor::ClearVoiceEditor (ClearVoiceProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);

    setupKnob (noise,  "noise",  "NOISE",  0.8);
    setupKnob (reverb, "reverb", "REVERB", 0.5);
    setupKnob (volume, "volume", "VOLUME", 0.0);
    setupKnob (voice,  "voice",  "PITCH",  0.0);

    styleLabel (voiceTitle, "VOICE", 13.0f, textHi, juce::Justification::centredLeft);
    styleLabel (deepLabel,  "DEEP",  11.0f, textDim);
    styleLabel (highLabel,  "HIGH",  11.0f, textDim);
    styleLabel (calmLabel,  "calm",  11.0f, textDim);
    for (auto* l : { &voiceTitle, &deepLabel, &highLabel, &calmLabel }) addAndMakeVisible (l);

    addAndMakeVisible (voiceToggle);
    voiceToggleAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "voiceOn", voiceToggle);
    voiceToggle.onClick = [this] { updateVoiceEnabled(); };
    updateVoiceEnabled();

    setSize (560, 420);
}

ClearVoiceEditor::~ClearVoiceEditor() { setLookAndFeel (nullptr); }

void ClearVoiceEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& title, double defaultValue)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setColour (juce::Slider::textBoxTextColourId, textHi);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setDoubleClickReturnValue (true, defaultValue);
    k.slider.setMouseDragSensitivity (180);
    addAndMakeVisible (k.slider);

    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k.slider);

    styleLabel (k.name, title, 12.0f, textDim);
    addAndMakeVisible (k.name);
}

void ClearVoiceEditor::updateVoiceEnabled()
{
    const bool on = voiceToggle.getToggleState();
    voice.slider.setEnabled (on);
    voice.name.setAlpha (on ? 1.0f : 0.4f);
    deepLabel.setAlpha (on ? 1.0f : 0.4f);
    highLabel.setAlpha (on ? 1.0f : 0.4f);
    calmLabel.setAlpha (on ? 1.0f : 0.4f);
    voice.slider.setAlpha (on ? 1.0f : 0.6f);
}

void ClearVoiceEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (textHi);
    g.setFont (juce::Font (juce::FontOptions (18.0f)));
    g.drawText ("ClearVoice", 24, 14, 200, 28, juce::Justification::centredLeft);

    // voice panel
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedTop (232).reduced (16.0f, 8.0f), 14.0f);
}

void ClearVoiceEditor::resized()
{
    auto area = getLocalBounds().reduced (24, 0);
    area.removeFromTop (52);

    // top row: three knobs
    auto row = area.removeFromTop (172);
    const int w = row.getWidth() / 3;
    for (auto* k : { &noise, &reverb, &volume })
    {
        auto cell = row.removeFromLeft (w);
        k->name.setBounds (cell.removeFromTop (18));
        k->slider.setBounds (cell);
    }

    // voice panel
    auto panelArea = getLocalBounds().withTrimmedTop (232).reduced (16, 8).reduced (16, 10);
    auto header = panelArea.removeFromTop (28);
    voiceTitle.setBounds (header.removeFromLeft (120));
    voiceToggle.setBounds (header.removeFromRight (46).withSizeKeepingCentre (46, 24));

    auto knobArea = panelArea.withSizeKeepingCentre (200, panelArea.getHeight());
    voice.name.setBounds (knobArea.removeFromTop (16));
    voice.slider.setBounds (knobArea);

    deepLabel.setBounds (voice.slider.getX() - 56, voice.slider.getY() + 62, 50, 16);
    highLabel.setBounds (voice.slider.getRight() + 6, voice.slider.getY() + 62, 50, 16);
    calmLabel.setBounds (voice.slider.getBounds().getCentreX() - 25, voice.slider.getY() + 2, 50, 14);
}

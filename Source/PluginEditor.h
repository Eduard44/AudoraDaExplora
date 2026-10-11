#pragma once

#include "PluginProcessor.h"

// Flat, simple rotary knobs: a dark dial, a thin value arc and a pointer.
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (8.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float alpha = s.isEnabled() ? 1.0f : 0.35f;
        const auto accent = juce::Colour (0xff4fd1c5).withAlpha (alpha);

        // track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff2c333d).withAlpha (alpha));
        g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc (from the centre for the bipolar voice knob, from the start otherwise)
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float midAngle = startAngle + 0.5f * (endAngle - startAngle);
        juce::Path val;
        val.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                           bipolar ? std::min (midAngle, angle) : startAngle,
                           bipolar ? std::max (midAngle, angle) : angle, true);
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // dial
        const float dialR = radius - 11.0f;
        g.setColour (juce::Colour (0xff1b2027).withAlpha (alpha));
        g.fillEllipse (centre.x - dialR, centre.y - dialR, dialR * 2.0f, dialR * 2.0f);
        g.setColour (juce::Colour (0xff3a424e).withAlpha (alpha));
        g.drawEllipse (centre.x - dialR, centre.y - dialR, dialR * 2.0f, dialR * 2.0f, 1.0f);

        // pointer
        juce::Path p;
        p.addRoundedRectangle (-1.8f, -dialR + 4.0f, 3.6f, dialR * 0.38f, 1.8f);
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        g.setColour (on ? juce::Colour (0xff4fd1c5) : juce::Colour (0xff2c333d));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        const float d = r.getHeight() - 6.0f;
        g.setColour (on ? juce::Colour (0xff0e1318) : juce::Colour (0xff8a94a3));
        g.fillEllipse (on ? r.getRight() - d - 3.0f : r.getX() + 3.0f, r.getY() + 3.0f, d, d);
    }
};

class ClearVoiceEditor : public juce::AudioProcessorEditor
{
public:
    explicit ClearVoiceEditor (ClearVoiceProcessor&);
    ~ClearVoiceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& title, double defaultValue);
    void updateVoiceEnabled();

    ClearVoiceProcessor& processor;
    KnobLookAndFeel laf;
    Knob noise, reverb, volume, voice;
    juce::ToggleButton voiceToggle;
    juce::Label voiceTitle, deepLabel, highLabel, calmLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> voiceToggleAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClearVoiceEditor)
};

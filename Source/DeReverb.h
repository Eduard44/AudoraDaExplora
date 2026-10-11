#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

//==============================================================================
// Spectral late-reverb suppressor (Lebart-style). It estimates how much of the
// current spectrum is just the decaying tail of what was said ~60 ms ago and
// turns those bins down. One instance per channel.
class DeReverb
{
public:
    static constexpr int N    = 1024;
    static constexpr int HOP  = 256;
    static constexpr int BINS = N / 2 + 1;
    static constexpr int kLatency = N;

    DeReverb() : fft (10)
    {
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N));
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        delayFrames = std::max (1, (int) std::ceil (0.060 * sr / (double) HOP));

        // Power of the late tail relative to the power delayFrames ago, assuming
        // a room that rings for roughly 0.45 s (60 dB decay).
        const double delaySec = (double) delayFrames * (double) HOP / sr;
        decay = (float) std::pow (10.0, -6.0 * delaySec / 0.45);

        hist.assign ((size_t) ((delayFrames + 1) * BINS), 0.0f);
        histPos = 0;
        std::fill (inRing.begin(), inRing.end(), 0.0f);
        std::fill (ola.begin(), ola.end(), 0.0f);
        std::fill (gainSmooth.begin(), gainSmooth.end(), 1.0f);
        wp = 0;
        counter = 0;
        qr = qw = qcount = 0;
        for (int i = 0; i < HOP; ++i) pushOut (0.0f); // prefill so output never underruns
    }

    void setAmount (float a) { amount = juce::jlimit (0.0f, 1.0f, a); }

    float processSample (float x)
    {
        inRing[(size_t) wp] = x;
        wp = (wp + 1) % N;
        if (++counter >= HOP) { counter = 0; processFrame(); }
        return popOut();
    }

private:
    void pushOut (float v) { outQ[(size_t) qw] = v; qw = (qw + 1) % (int) outQ.size(); ++qcount; }
    float popOut()
    {
        if (qcount <= 0) return 0.0f;
        const float v = outQ[(size_t) qr];
        qr = (qr + 1) % (int) outQ.size();
        --qcount;
        return v;
    }

    void processFrame()
    {
        for (int i = 0; i < N; ++i)
            fftData[(size_t) i] = inRing[(size_t) ((wp + i) % N)] * win[(size_t) i];
        std::fill (fftData.begin() + N, fftData.end(), 0.0f);

        fft.performRealOnlyForwardTransform (fftData.data(), true);

        const size_t slotRead  = (size_t) ((histPos + 1) % (delayFrames + 1)) * BINS;
        const size_t slotWrite = (size_t) histPos * BINS;
        const float lambda = amount * 2.0f;
        constexpr float gMin = 0.1f; // never turn a bin down by more than 20 dB

        for (int k = 0; k < BINS; ++k)
        {
            const float re = fftData[(size_t) (2 * k)], im = fftData[(size_t) (2 * k + 1)];
            const float pw = re * re + im * im;
            const float late = decay * hist[slotRead + (size_t) k];
            hist[slotWrite + (size_t) k] = pw;

            const float g2 = 1.0f - lambda * late / (pw + 1e-12f);
            gainRaw[(size_t) k] = std::sqrt (std::max (g2, gMin * gMin));
        }

        // smooth across frequency, then across time (fast to turn down, slow to open up)
        for (int k = 0; k < BINS; ++k)
        {
            const float a = gainRaw[(size_t) std::max (k - 1, 0)];
            const float b = gainRaw[(size_t) k];
            const float c = gainRaw[(size_t) std::min (k + 1, BINS - 1)];
            const float g = 0.25f * a + 0.5f * b + 0.25f * c;
            float& s = gainSmooth[(size_t) k];
            s += (g - s) * (g < s ? 0.7f : 0.3f);
        }

        for (int k = 0; k < BINS; ++k)
        {
            fftData[(size_t) (2 * k)]     *= gainSmooth[(size_t) k];
            fftData[(size_t) (2 * k + 1)] *= gainSmooth[(size_t) k];
        }

        fft.performRealOnlyInverseTransform (fftData.data());

        constexpr float scale = 0.5f; // sqrt-Hann analysis + synthesis at 75 % overlap sums to 2
        for (int i = 0; i < N; ++i)
            ola[(size_t) i] += fftData[(size_t) i] * win[(size_t) i] * scale;

        for (int i = 0; i < HOP; ++i) pushOut (ola[(size_t) i]);
        std::copy (ola.begin() + HOP, ola.end(), ola.begin());
        std::fill (ola.end() - HOP, ola.end(), 0.0f);

        histPos = (histPos + 1) % (delayFrames + 1);
    }

    juce::dsp::FFT fft;
    double sr = 48000.0;
    float amount = 0.0f, decay = 0.25f;
    int delayFrames = 12, histPos = 0, wp = 0, counter = 0;
    int qr = 0, qw = 0, qcount = 0;

    std::array<float, N> win {}, inRing {}, ola {};
    std::array<float, 2 * N> fftData {};
    std::array<float, BINS> gainRaw {}, gainSmooth {};
    std::array<float, 4096> outQ {};
    std::vector<float> hist;
};

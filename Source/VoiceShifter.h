#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

//==============================================================================
// Formant-aware pitch shifter. A phase vocoder moves the pitch, but the spectral
// envelope (the "shape" of the voice) is separated out first, moved by only part
// of the pitch change, and put back. That is what keeps a lower voice sounding
// like a bigger person and a higher one like a smaller person, rather than a
// monster or a chipmunk. One instance per channel.
class VoiceShifter
{
public:
    static constexpr int N     = 2048;
    static constexpr int OSAMP = 4;
    static constexpr int HOP   = N / OSAMP;
    static constexpr int BINS  = N / 2 + 1;
    static constexpr int kLatency = N;

    // 0 = formants never move (voice "shape" fixed), 1 = formants move with pitch.
    static constexpr double kFormantFollow = 0.55;

    VoiceShifter() : fft (11)
    {
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        lifter = std::max (8, (int) (0.002 * sr));
        levelCoef = 1.0 - std::exp (-1.0 / (0.15 * sr));
        inEnergy = outEnergy = 0.0;
        levelGain = 1.0;
        std::fill (inRing.begin(), inRing.end(), 0.0f);
        std::fill (ola.begin(), ola.end(), 0.0f);
        lastPhase.fill (0.0);
        sumPhase.fill (0.0);
        wp = counter = 0;
        qr = qw = qcount = 0;
        for (int i = 0; i < HOP; ++i) pushOut (0.0f);
    }

    void setRatio (double r) { ratio = juce::jlimit (0.25, 4.0, r); }

    float processSample (float x)
    {
        const float delayedIn = inRing[(size_t) wp]; // the input from N samples ago, time-aligned with the output
        inRing[(size_t) wp] = x;
        wp = (wp + 1) % N;
        if (++counter >= HOP) { counter = 0; processFrame(); }
        const float y = popOut();

        // Keep the shifted voice at the same loudness as the original. Pitch shifting
        // with a phase vocoder loses a few dB, so follow both levels slowly and correct.
        inEnergy  += ((double) delayedIn * delayedIn - inEnergy) * levelCoef;
        outEnergy += ((double) y * y - outEnergy) * levelCoef;
        const double target = std::min (2.5, std::max (0.6, std::sqrt ((inEnergy + 1e-9) / (outEnergy + 1e-9))));
        levelGain += (target - levelGain) * levelCoef * 4.0;
        return (float) (y * levelGain);
    }

private:
    static double wrapPi (double x)
    {
        constexpr double twoPi = 6.283185307179586;
        return x - twoPi * std::floor (x / twoPi + 0.5);
    }

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
        constexpr double pi = 3.14159265358979323846;
        constexpr double expct = 2.0 * pi * (double) HOP / (double) N;

        // ---- analysis ----
        for (int i = 0; i < N; ++i)
            fftData[(size_t) i] = inRing[(size_t) ((wp + i) % N)] * win[(size_t) i];
        std::fill (fftData.begin() + N, fftData.end(), 0.0f);
        fft.performRealOnlyForwardTransform (fftData.data(), true);

        for (int k = 0; k < BINS; ++k)
        {
            const double re = fftData[(size_t) (2 * k)], im = fftData[(size_t) (2 * k + 1)];
            mag[(size_t) k] = std::sqrt (re * re + im * im);
            const double ph = std::atan2 (im, re);
            anaPhase[(size_t) k] = ph;

            double delta = ph - lastPhase[(size_t) k];
            lastPhase[(size_t) k] = ph;
            delta = wrapPi (delta - (double) k * expct);
            anaFreq[(size_t) k] = (double) k + (double) OSAMP * delta / (2.0 * pi);
        }

        // ---- spectral envelope via cepstral smoothing ----
        std::fill (cep.begin(), cep.end(), 0.0f);
        for (int k = 0; k < BINS; ++k)
            cep[(size_t) (2 * k)] = (float) std::log (mag[(size_t) k] + 1e-9);
        fft.performRealOnlyInverseTransform (cep.data());
        for (int n = lifter; n <= N - lifter; ++n) cep[(size_t) n] = 0.0f;
        std::fill (cep.begin() + N, cep.end(), 0.0f);
        fft.performRealOnlyForwardTransform (cep.data(), true);
        for (int k = 0; k < BINS; ++k)
            env[(size_t) k] = std::exp ((double) cep[(size_t) (2 * k)]);

        // ---- pitch shift on the flattened spectrum ----
        const double fr = std::pow (ratio, kFormantFollow);
        for (int j = 0; j < BINS; ++j)
        {
            const int src = (int) std::floor ((double) j / ratio + 0.5);
            if (src >= BINS) { synMag[(size_t) j] = 0.0; srcIdx[(size_t) j] = BINS - 1; synFreq[(size_t) j] = (double) j; continue; }
            srcIdx[(size_t) j] = src;
            synMag[(size_t) j]  = mag[(size_t) src] / env[(size_t) src];
            synFreq[(size_t) j] = anaFreq[(size_t) src] * ratio;
        }

        // phase propagation
        for (int j = 0; j < BINS; ++j)
        {
            double tmp = synFreq[(size_t) j] - (double) j;
            tmp *= 2.0 * pi / (double) OSAMP;
            tmp += (double) j * expct;
            accPhase[(size_t) j] = sumPhase[(size_t) j] + tmp;
        }

        // identity phase locking around spectral peaks keeps the voice from sounding phasey
        peaks.clear();
        for (int j = 2; j < BINS - 2; ++j)
        {
            const double m = synMag[(size_t) j];
            if (m > synMag[(size_t) (j - 1)] && m > synMag[(size_t) (j - 2)]
                && m >= synMag[(size_t) (j + 1)] && m >= synMag[(size_t) (j + 2)])
                peaks.push_back (j);
        }

        if (peaks.empty())
        {
            sumPhase = accPhase;
        }
        else
        {
            for (size_t pi_ = 0; pi_ < peaks.size(); ++pi_)
            {
                const int p  = peaks[pi_];
                const int lo = pi_ == 0 ? 0 : (peaks[pi_ - 1] + p) / 2 + 1;
                const int hi = pi_ + 1 == peaks.size() ? BINS - 1 : (p + peaks[pi_ + 1]) / 2;
                for (int j = lo; j <= hi; ++j)
                    sumPhase[(size_t) j] = accPhase[(size_t) p]
                                         + (anaPhase[(size_t) srcIdx[(size_t) j]] - anaPhase[(size_t) srcIdx[(size_t) p]]);
            }
        }
        for (int j = 0; j < BINS; ++j) sumPhase[(size_t) j] = wrapPi (sumPhase[(size_t) j]);

        // ---- synthesis: put the (moved) envelope back ----
        for (int j = 0; j < BINS; ++j)
        {
            const double pos = (double) j / fr;
            const int i0 = (int) std::floor (pos);
            double e;
            if (i0 >= BINS - 1) e = i0 >= BINS ? 1e-9 : env[(size_t) (BINS - 1)];
            else { const double f = pos - (double) i0; e = env[(size_t) i0] * (1.0 - f) + env[(size_t) (i0 + 1)] * f; }
            envShift[(size_t) j] = e;
        }

        for (int j = 0; j < BINS; ++j)
        {
            const double m = synMag[(size_t) j] * envShift[(size_t) j];
            fftData[(size_t) (2 * j)]     = (float) (m * std::cos (sumPhase[(size_t) j]));
            fftData[(size_t) (2 * j + 1)] = (float) (m * std::sin (sumPhase[(size_t) j]));
        }
        fftData[1] = 0.0f;
        fftData[(size_t) (2 * (BINS - 1) + 1)] = 0.0f;

        fft.performRealOnlyInverseTransform (fftData.data());

        constexpr float scale = 1.0f / 1.5f; // Hann analysis * Hann synthesis at 75 % overlap sums to 1.5
        for (int i = 0; i < N; ++i)
            ola[(size_t) i] += fftData[(size_t) i] * win[(size_t) i] * scale;

        for (int i = 0; i < HOP; ++i) pushOut (ola[(size_t) i]);
        std::copy (ola.begin() + HOP, ola.end(), ola.begin());
        std::fill (ola.end() - HOP, ola.end(), 0.0f);
    }

    juce::dsp::FFT fft;
    double sr = 48000.0, ratio = 1.0;
    int lifter = 96, wp = 0, counter = 0, qr = 0, qw = 0, qcount = 0;

    std::array<float, N> win {}, inRing {}, ola {};
    std::array<float, 2 * N> fftData {}, cep {};
    std::array<float, 8192> outQ {};
    double inEnergy = 0.0, outEnergy = 0.0, levelGain = 1.0, levelCoef = 0.0001;
    std::array<double, BINS> mag {}, anaPhase {}, anaFreq {}, env {}, envShift {}, synMag {}, synFreq {}, accPhase {};
    std::array<double, BINS> lastPhase {}, sumPhase {};
    std::array<int, BINS> srcIdx {};
    std::vector<int> peaks;
};

//==============================================================================
// The "calm" finish used when the Voice section is on: it takes the edge off
// harsh S sounds, steadies loud peaks and softens the top end a touch.
class CalmStage
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        auto shelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 6500.0, 0.7f, juce::Decibels::decibelsToGain (-1.5f));
        auto hp    = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 5500.0, 0.707f);
        tilt.coefficients = shelf;
        sibilance.coefficients = hp;
        tilt.reset();
        sibilance.reset();
        sibEnv = levEnv = 0.0f;

        sibAtk = std::exp (-1.0f / (0.001f * (float) sr));
        sibRel = std::exp (-1.0f / (0.040f * (float) sr));
        levAtk = std::exp (-1.0f / (0.020f * (float) sr));
        levRel = std::exp (-1.0f / (0.250f * (float) sr));
    }

    float process (float x)
    {
        // 1. de-ess: pull back only the sibilant band, and only when it spikes
        const float s = sibilance.processSample (x);
        const float a = std::abs (s);
        sibEnv = a > sibEnv ? a + sibAtk * (sibEnv - a) : a + sibRel * (sibEnv - a);
        constexpr float sibThresh = 0.02f; // about -34 dBFS
        const float sibGain = sibEnv > sibThresh ? std::pow (sibThresh / sibEnv, 0.6f) : 1.0f;
        float y = x - s * (1.0f - sibGain);

        // 2. gentle 2:1 leveller above about -22 dBFS
        const float b = std::abs (y);
        levEnv = b > levEnv ? b + levAtk * (levEnv - b) : b + levRel * (levEnv - b);
        constexpr float levThresh = 0.08f;
        const float levGain = levEnv > levThresh ? std::pow (levThresh / levEnv, 0.5f) : 1.0f;
        y *= levGain;

        // 3. soften the top end slightly
        return tilt.processSample (y);
    }

private:
    double sr = 48000.0;
    juce::dsp::IIR::Filter<float> tilt, sibilance;
    float sibEnv = 0, levEnv = 0, sibAtk = 0, sibRel = 0, levAtk = 0, levRel = 0;
};

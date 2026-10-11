#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

extern "C" {
#include "rnnoise.h"
}

//==============================================================================
// Streaming windowed-sinc resampler (used only when the host is not at 48 kHz,
// because RNNoise works at exactly 48 kHz).
class StreamResampler
{
public:
    static constexpr int H = 24; // half-width of the kernel, in input samples

    void prepare (double inRate, double outRate)
    {
        step = inRate / outRate;
        fc   = std::min (1.0, outRate / inRate) * 0.95;
        reset();
    }

    void reset()
    {
        buf.assign ((size_t) H, 0.0f);
        pos = (double) H;
    }

    void push (const float* x, int n) { buf.insert (buf.end(), x, x + n); }

    // Appends every output sample that can currently be produced.
    void pull (std::vector<float>& out)
    {
        const double pi = 3.14159265358979323846;

        while ((int) pos + H < (int) buf.size())
        {
            const int center = (int) pos;
            const double frac = pos - (double) center;

            double sum = 0.0, wsum = 0.0;
            for (int k = 1 - H; k <= H; ++k)
            {
                const double x = (double) k - frac;
                const double t = fc * x;
                const double s = std::abs (t) < 1e-9 ? 1.0 : std::sin (pi * t) / (pi * t);
                const double w = 0.5 + 0.5 * std::cos (pi * x / (double) H);
                const double c = fc * s * w;
                sum  += c * (double) buf[(size_t) (center + k)];
                wsum += c;
            }
            out.push_back ((float) (sum / wsum));
            pos += step;
        }

        const int drop = (int) pos - H;
        if (drop > 0)
        {
            buf.erase (buf.begin(), buf.begin() + drop);
            pos -= (double) drop;
        }
    }

private:
    std::vector<float> buf;
    double pos = (double) H, step = 1.0, fc = 1.0;
};

//==============================================================================
// Real-time neural noise suppression (RNNoise, BSD-3-Clause) for one channel.
class NoiseReducer
{
public:
    static constexpr int kFrame = 480;

    // Extra delay (in 48 kHz samples) that RNNoise adds on top of its 480-sample
    // frame buffering. Measured in the test harness, see Tests/.
    static constexpr int kIntrinsic48 = 480;

    NoiseReducer() = default;
    ~NoiseReducer() { if (state != nullptr) rnnoise_destroy (state); }
    NoiseReducer (const NoiseReducer&) = delete;
    NoiseReducer& operator= (const NoiseReducer&) = delete;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        direct = std::abs (sr - 48000.0) < 0.5;

        if (state != nullptr) rnnoise_destroy (state);
        state = rnnoise_create (nullptr);

        in48.clear();
        out48.clear();
        in48.reserve ((size_t) (maxBlock * 2 + 4096));
        out48.reserve ((size_t) (kFrame * 4));

        if (! direct)
        {
            down.prepare (sr, 48000.0);
            up.prepare (48000.0, sr);
        }

        prefill = (int) std::ceil ((double) kFrame * sr / 48000.0) + 32;
        if (! direct)
            prefill += 2 * (int) std::ceil ((double) StreamResampler::H * (1.0 + sr / 48000.0)) + 16;
        outHost.assign ((size_t) prefill, 0.0f);
        outHost.reserve ((size_t) (prefill + maxBlock * 3 + 4096));
        underruns = 0;
    }

    // Total delay between input and output, in host-rate samples.
    int getLatency() const
    {
        // The resamplers are zero-phase (they only need lookahead, which the prefill
        // covers), so the signal delay is the prefill plus RNNoise's own delay.
        return (int) std::lround ((double) prefill + (double) kIntrinsic48 * sr / 48000.0);
    }

    int getUnderrunCount() const { return underruns; }

    void process (const float* in, float* out, int n)
    {
        if (direct) in48.insert (in48.end(), in, in + n);
        else { down.push (in, n); down.pull (in48); }

        while ((int) in48.size() >= kFrame)
        {
            float frameIn[kFrame], frameOut[kFrame];
            for (int i = 0; i < kFrame; ++i) frameIn[i] = in48[(size_t) i] * 32768.0f;
            rnnoise_process_frame (state, frameOut, frameIn);
            for (int i = 0; i < kFrame; ++i) out48.push_back (frameOut[i] * (1.0f / 32768.0f));
            in48.erase (in48.begin(), in48.begin() + kFrame);
        }

        if (direct) outHost.insert (outHost.end(), out48.begin(), out48.end());
        else { up.push (out48.data(), (int) out48.size()); up.pull (outHost); }
        out48.clear();

        size_t r = 0;
        for (int i = 0; i < n; ++i)
        {
            if (r < outHost.size()) out[i] = outHost[r++];
            else { out[i] = 0.0f; ++underruns; }
        }
        outHost.erase (outHost.begin(), outHost.begin() + (long) r);
    }

private:
    DenoiseState* state = nullptr;
    double sr = 48000.0;
    bool direct = true;
    int prefill = 0, underruns = 0;
    StreamResampler down, up;
    std::vector<float> in48, out48, outHost;
};

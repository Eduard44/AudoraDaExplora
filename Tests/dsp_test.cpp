#include <juce_dsp/juce_dsp.h>
#include "NoiseReducer.h"
#include "DeReverb.h"
#include "VoiceShifter.h"
#include <cstdio>
#include <random>

static std::vector<float> voiced (double sr, double seconds, double f0)
{
    int n = (int) (sr * seconds);
    std::vector<float> x ((size_t) n);
    std::mt19937 rng (1);
    std::normal_distribution<float> nd (0.f, 1.f);
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        double t = i / sr;
        double f = f0 * (1.0 + 0.01 * std::sin (2 * M_PI * 5 * t));
        ph += 2 * M_PI * f / sr;
        double s = 0;
        for (int h = 1; h <= 30; ++h)
        {
            double fh = h * f0;
            double env = std::exp (-std::pow ((fh - 700) / 400, 2)) + 0.7 * std::exp (-std::pow ((fh - 1800) / 600, 2)) + 0.05;
            s += env * std::sin (h * ph) / h * 3.0;
        }
        double am = 0.6 + 0.4 * std::sin (2 * M_PI * 3 * t);
        x[(size_t) i] = (float) (0.25 * s * am);
    }
    return x;
}

// best lag (in samples) of y relative to x: y[n] ~ x[n - lag]
static int bestLag (const std::vector<float>& x, const std::vector<float>& y, int maxLag)
{
    int best = 0; double bv = -1e30;
    for (int lag = 0; lag < maxLag; ++lag)
    {
        double s = 0;
        for (size_t n = (size_t) maxLag; n < x.size() - 1; n += 3) s += (double) y[n] * x[n - (size_t) lag];
        if (s > bv) { bv = s; best = lag; }
    }
    return best;
}

static double rms (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (b - a));
}

// fundamental via autocorrelation on the tail of the signal
static double estF0 (const std::vector<float>& y, double sr, double lo, double hi)
{
    size_t a = y.size() / 2, len = 8192;
    double best = -1e30; int bl = 0;
    for (int lag = (int) (sr / hi); lag < (int) (sr / lo); ++lag)
    {
        double s = 0, e1 = 0, e2 = 0;
        for (size_t i = a; i < a + len; ++i) { s += (double) y[i] * y[i + (size_t) lag]; e1 += (double) y[i] * y[i]; e2 += (double) y[i + (size_t) lag] * y[i + (size_t) lag]; }
        double c = s / std::sqrt (e1 * e2 + 1e-12);
        if (c > best) { best = c; bl = lag; }
    }
    return sr / bl;
}

int main()
{
    // ---- NoiseReducer ----
    for (double sr : { 48000.0, 44100.0, 96000.0 })
    {
        auto x = voiced (sr, 4.0, 140.0);
        NoiseReducer nr; nr.prepare (sr, 512);
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); i += 512)
        {
            int n = (int) std::min<size_t> (512, x.size() - i);
            nr.process (x.data() + i, y.data() + i, n);
        }
        int lag = bestLag (x, y, (int) (0.1 * sr));
        std::printf ("NoiseReducer sr=%.0f underruns=%d measuredLag=%d reported=%d rmsIn=%.4f rmsOut=%.4f\n",
                     sr, nr.getUnderrunCount(), lag, nr.getLatency(), rms (x, x.size() / 2, x.size()), rms (y, x.size() / 2, x.size()));
    }

    // ---- DeReverb ----
    for (double sr : { 48000.0, 44100.0 })
    {
        auto x = voiced (sr, 3.0, 140.0);
        std::vector<float> y (x.size());
        auto dr = std::make_unique<DeReverb>(); dr->prepare (sr); dr->setAmount (0.0f);
        for (size_t i = 0; i < x.size(); ++i) y[i] = dr->processSample (x[i]);
        int lag = bestLag (x, y, 2000);
        double err = 0; for (size_t i = 4000; i < x.size(); ++i) err = std::max (err, (double) std::abs (y[i] - x[i - (size_t) DeReverb::kLatency]));
        std::printf ("DeReverb amount0 sr=%.0f lag=%d (expect %d) maxErr=%.6f\n", sr, lag, DeReverb::kLatency, err);

        // reverb tail test: add decaying noise-ish tail
        std::vector<float> rev (x.size(), 0.f); std::mt19937 rng (3); std::normal_distribution<float> nd (0, 1);
        std::vector<float> ir ((size_t) (0.5 * sr)); for (size_t i = 0; i < ir.size(); ++i) ir[i] = nd (rng) * std::exp (-6.9f * (float) i / (0.4f * (float) sr)) * 0.02f;
        for (size_t i = 0; i < x.size(); ++i) { double s = x[i]; for (size_t k = (size_t) (0.02 * sr); k < ir.size() && k <= i; k += 7) s += (double) ir[k] * x[i - k] * 7; rev[i] = (float) s; }
        std::vector<float> o (x.size()); auto d2 = std::make_unique<DeReverb>(); d2->prepare (sr); d2->setAmount (1.0f);
        for (size_t i = 0; i < x.size(); ++i) o[i] = d2->processSample (rev[i]);
        bool bad = false; for (float v : o) if (! std::isfinite (v)) bad = true;
        std::printf ("DeReverb amount1 finite=%d rmsIn=%.4f rmsOut=%.4f\n", ! bad, rms (rev, 20000, rev.size()), rms (o, 20000, o.size()));
    }

    // ---- VoiceShifter ----
    for (double ratio : { 1.0, 0.7492, 1.3348 })
    {
        double sr = 48000.0;
        auto x = voiced (sr, 4.0, 150.0);
        auto vs = std::make_unique<VoiceShifter>(); vs->prepare (sr); vs->setRatio (ratio);
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = vs->processSample (x[i]);
        bool bad = false; for (float v : y) if (! std::isfinite (v)) bad = true;
        std::printf ("VoiceShifter ratio=%.4f finite=%d f0in=%.1f f0out=%.1f (expect %.1f) rmsIn=%.4f rmsOut=%.4f lag=%d\n",
                     ratio, ! bad, estF0 (x, sr, 80, 400), estF0 (y, sr, 80, 400), 150.0 * ratio,
                     rms (x, x.size() / 2, x.size()), rms (y, x.size() / 2, x.size()), bestLag (x, y, 4000));
    }

    // ---- CalmStage ----
    {
        auto x = voiced (48000.0, 1.0, 140.0); CalmStage c; c.prepare (48000.0);
        bool bad = false; double mx = 0; for (float v : x) { float o = c.process (v); if (! std::isfinite (o)) bad = true; mx = std::max (mx, (double) std::abs (o)); }
        std::printf ("CalmStage finite=%d peakOut=%.3f\n", ! bad, mx);
    }
    return 0;
}

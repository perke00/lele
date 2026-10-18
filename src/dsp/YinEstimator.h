#pragma once
// ATLAS DSP core - YIN fundamental frequency estimator (de Cheveigne & Kawahara, 2002).
//
// Real-time contract:
//   prepare()  allocates; call it off the audio thread.
//   process()  performs NO allocation, NO locks, NO I/O. Cost is O(window * maxLag).
//
// The estimator works on one frame of `frameSize()` samples and returns a frequency
// plus a confidence in [0,1] (1 - cumulative-mean-normalised difference at the chosen lag).

#include <algorithm>
#include <cmath>
#include <vector>

namespace atlas::dsp {

struct YinResult
{
    float frequencyHz = 0.0f; // 0 when nothing usable was found
    float confidence = 0.0f;  // 0..1, higher is more periodic
    bool  voiced = false;     // true only if the CMND dipped below the threshold
};

class YinEstimator
{
public:
    void prepare(double sampleRate, double minHz, double maxHz, float threshold = 0.2f)
    {
        sampleRate_ = sampleRate;
        threshold_ = threshold;
        tauMin_ = std::max(2, static_cast<int>(std::floor(sampleRate / maxHz)));
        tauMax_ = std::max(tauMin_ + 3, static_cast<int>(std::ceil(sampleRate / minHz)));
        window_ = tauMax_;
        frameSize_ = window_ + tauMax_;
        cmnd_.assign(static_cast<std::size_t>(tauMax_) + 1, 1.0f);
        scratch_.assign(static_cast<std::size_t>(frameSize_), 0.0f);
    }

    int frameSize() const noexcept { return frameSize_; }

    // `frame` must point at frameSize() valid samples.
    YinResult process(const float* frame) noexcept
    {
        YinResult r;
        if (frameSize_ == 0)
            return r;

        // Remove DC so offsets can't masquerade as periodicity.
        double mean = 0.0;
        for (int i = 0; i < frameSize_; ++i) mean += frame[i];
        mean /= frameSize_;
        float* x = scratch_.data();
        for (int i = 0; i < frameSize_; ++i) x[i] = static_cast<float>(frame[i] - mean);

        // Difference function + cumulative mean normalisation (YIN steps 2-3).
        cmnd_[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax_; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < window_; ++j)
            {
                const double diff = static_cast<double>(x[j]) - static_cast<double>(x[j + tau]);
                d += diff * diff;
            }
            running += d;
            cmnd_[static_cast<std::size_t>(tau)] = running > 1e-20 ? static_cast<float>(d * tau / running) : 1.0f;
        }

        // Absolute threshold: first lag under threshold, walked down to its local minimum.
        int best = -1;
        for (int t = tauMin_; t < tauMax_; ++t)
        {
            if (cmnd_[static_cast<std::size_t>(t)] < threshold_)
            {
                while (t + 1 < tauMax_ && cmnd_[static_cast<std::size_t>(t + 1)] < cmnd_[static_cast<std::size_t>(t)])
                    ++t;
                best = t;
                r.voiced = true;
                break;
            }
        }
        if (best < 0) // fall back to global minimum, flagged unvoiced
        {
            best = tauMin_;
            for (int t = tauMin_; t < tauMax_; ++t)
                if (cmnd_[static_cast<std::size_t>(t)] < cmnd_[static_cast<std::size_t>(best)])
                    best = t;
        }

        // Parabolic interpolation around the minimum for sub-sample lag accuracy.
        const float s0 = cmnd_[static_cast<std::size_t>(best - 1)];
        const float s1 = cmnd_[static_cast<std::size_t>(best)];
        const float s2 = cmnd_[static_cast<std::size_t>(best + 1)];
        const float denom = s0 - 2.0f * s1 + s2;
        float shift = 0.0f;
        if (std::fabs(denom) > 1e-12f)
            shift = std::clamp(0.5f * (s0 - s2) / denom, -1.0f, 1.0f);

        const double lag = static_cast<double>(best) + static_cast<double>(shift);
        r.frequencyHz = static_cast<float>(sampleRate_ / lag);
        r.confidence = std::clamp(1.0f - s1, 0.0f, 1.0f);
        return r;
    }

private:
    double sampleRate_ = 0.0;
    float  threshold_ = 0.2f;
    int    tauMin_ = 0, tauMax_ = 0, window_ = 0, frameSize_ = 0;
    std::vector<float> cmnd_;
    std::vector<float> scratch_;
};

} // namespace atlas::dsp

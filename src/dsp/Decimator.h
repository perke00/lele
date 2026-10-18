#pragma once
// ATLAS DSP core - offline linear-phase FIR low-pass + integer decimation.
//
// Used as the front-end of pitch analysis: kick fundamentals live below ~400 Hz,
// so analysing at ~4 kHz is ~100x cheaper than at 44.1/48/96 kHz with no loss of
// useful information. NOT real-time safe (allocates); analysis runs off the audio thread.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace atlas::dsp {

class Decimator
{
public:
    static int chooseFactor(double sampleRate, double targetRate = 4000.0) noexcept
    {
        return std::max(1, static_cast<int>(std::floor(sampleRate / targetRate)));
    }

    // Returns y[m] = (lowpass(x))[m * factor], zero-phase (group delay compensated).
    // Cutoff is 25% of the *output* rate; transition band of the Blackman design
    // (~5.5/N cycles/sample) stays below the output Nyquist.
    static std::vector<float> process(const float* in, std::size_t n, int factor, double /*sampleRate*/)
    {
        if (factor <= 1)
            return std::vector<float>(in, in + n);

        const int taps = 16 * factor + 1; // odd => integer group delay
        const int half = taps / 2;
        const double fc = 0.25 / static_cast<double>(factor); // cycles/sample (input rate)

        std::vector<double> h(static_cast<std::size_t>(taps));
        double sum = 0.0;
        for (int k = 0; k < taps; ++k)
        {
            const double m = k - half;
            const double sinc = (m == 0.0) ? 2.0 * fc
                                           : std::sin(2.0 * std::numbers::pi * fc * m) / (std::numbers::pi * m);
            const double w = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * k / (taps - 1))
                                  + 0.08 * std::cos(4.0 * std::numbers::pi * k / (taps - 1));
            h[static_cast<std::size_t>(k)] = sinc * w;
            sum += h[static_cast<std::size_t>(k)];
        }
        for (auto& v : h) v /= sum; // unity DC gain

        const std::size_t outLen = (n + static_cast<std::size_t>(factor) - 1) / static_cast<std::size_t>(factor);
        std::vector<float> out(outLen);
        const long nn = static_cast<long>(n);

        for (std::size_t m = 0; m < outLen; ++m)
        {
            const long centre = static_cast<long>(m) * factor;
            double acc = 0.0;
            for (int k = -half; k <= half; ++k)
            {
                const long idx = centre - k;
                if (idx >= 0 && idx < nn)
                    acc += h[static_cast<std::size_t>(k + half)] * static_cast<double>(in[idx]);
            }
            out[m] = static_cast<float>(acc);
        }
        return out;
    }
};

} // namespace atlas::dsp

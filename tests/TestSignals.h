#pragma once
// Deterministic synthetic signals with KNOWN ground truth for DSP tests.

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace atlas::test {

struct Rng // xorshift32, deterministic across platforms
{
    std::uint32_t s;
    explicit Rng(std::uint32_t seed = 12345u) : s(seed) {}
    float next() // uniform in [-1, 1)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s) / 2147483648.0f - 1.0f;
    }
};

inline std::vector<float> makeSine(double sr, double hz, double seconds, float amp = 0.8f)
{
    std::vector<float> v(static_cast<std::size_t>(seconds * sr));
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = amp * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sr));
    return v;
}

inline std::vector<float> makeNoise(double sr, double seconds, float amp = 0.5f, std::uint32_t seed = 1u)
{
    Rng r(seed);
    std::vector<float> v(static_cast<std::size_t>(seconds * sr));
    for (auto& x : v) x = amp * r.next();
    return v;
}

struct KickParams
{
    double sr = 44100.0;
    double durationSec = 0.6;
    double startHz = 200.0;     // pitch at t=0
    double endHz = 50.0;        // settled pitch  <- ground truth for tests
    double pitchTauSec = 0.02;  // pitch envelope time constant
    double ampTauSec = 0.25;    // amplitude decay time constant
    double harmonic2 = 0.0;     // level of 2nd harmonic (relative to fundamental)
    double drive = 0.0;         // tanh drive (0 = clean)
    double noise = 0.0;         // broadband noise level during body
    double click = 0.0;         // 1.5 ms noise burst at the start
    std::uint32_t seed = 7u;
};

inline std::vector<float> makeKick(const KickParams& p)
{
    const std::size_t n = static_cast<std::size_t>(p.durationSec * p.sr);
    std::vector<float> v(n);
    Rng rng(p.seed);
    double phase = 0.0;
    const std::size_t clickLen = static_cast<std::size_t>(0.0015 * p.sr);

    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / p.sr;
        const double f = p.endHz + (p.startHz - p.endHz) * std::exp(-t / p.pitchTauSec);
        phase += 2.0 * std::numbers::pi * f / p.sr;
        const double env = std::exp(-t / p.ampTauSec);

        double x = std::sin(phase) + p.harmonic2 * std::sin(2.0 * phase);
        if (p.drive > 0.0)
            x = std::tanh(p.drive * x) / std::tanh(p.drive);
        x *= env;
        x += p.noise * env * static_cast<double>(rng.next());
        if (i < clickLen)
            x += p.click * static_cast<double>(rng.next());
        v[i] = static_cast<float>(x);
    }

    float peak = 0.0f;
    for (float s : v) peak = std::fmax(peak, std::fabs(s));
    if (peak > 0.0f)
        for (auto& s : v) s *= 0.9f / peak;
    return v;
}

inline double centsBetween(double measuredHz, double trueHz)
{
    return 1200.0 * std::log2(measuredHz / trueHz);
}

} // namespace atlas::test

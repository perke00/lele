#pragma once
// ATLAS DSP core - kick-drum pitch analysis (offline, run on sample load / worker thread).
//
// Pipeline: find the onset (first sample >= 10% of peak) -> low-pass + decimate to ~4 kHz -> run YIN on
// overlapping frames along the body -> gate out quiet frames -> decide reliability.
//
// Design rules (from the project brief):
//   * Never report a note unless the analysis is reliable. When it is not, `reliable`
//     is false, frequency/note fields are zero and `status` says why.
//   * The per-frame pitch track is always returned so the GUI can draw it.
//   * "Body pitch" = median of the voiced frames in the LATER HALF of the voiced region.
//     Kicks sweep down from a high start pitch; the settled pitch is what a producer tunes.
//   * LIMITATION: analysis frames are ~2/minHz long (~70 ms at 28 Hz), so a fast pitch sweep
//     is smeared across the first frames. Those frames come out unvoiced (low confidence) and
//     are excluded; the first TRUSTWORTHY pitch is reported as `firstVoicedHz`, which can be
//     well below the true starting pitch of the sweep. It is not a measurement of the sweep start.

#include "Decimator.h"
#include "NoteUtils.h"
#include "YinEstimator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace atlas::dsp {

enum class PitchStatus
{
    Ok,
    InvalidInput,  // empty buffer, bad sample rate, or NaN/Inf samples
    Silent,        // peak below -80 dBFS
    TooShort,      // not enough audio after the transient for one analysis frame
    NoPitchFound,  // noisy / unpitched: too few frames were periodic
    Unstable       // periodic frames exist but disagree (octave jumps, heavy modulation)
};

inline const char* toString(PitchStatus s) noexcept
{
    switch (s)
    {
        case PitchStatus::Ok:           return "Pitch detected";
        case PitchStatus::InvalidInput: return "Invalid audio data";
        case PitchStatus::Silent:       return "Sample is silent";
        case PitchStatus::TooShort:     return "Sample too short to analyse";
        case PitchStatus::NoPitchFound: return "No stable pitch (noisy or unpitched)";
        case PitchStatus::Unstable:     return "Pitch unstable - set tuning manually";
    }
    return "";
}

struct PitchFrame
{
    double timeSec = 0.0;      // frame centre, seconds from start of the analysed buffer
    float  frequencyHz = 0.0f; // raw YIN output for this frame
    float  confidence = 0.0f;
    float  rmsDb = -200.0f;    // frame level in the decimated domain (relative, dBFS-like)
    bool   gated = false;      // true if loud enough to be considered
    bool   voiced = false;     // gated AND periodic
};

struct KickPitchResult
{
    bool        reliable = false;
    PitchStatus status = PitchStatus::InvalidInput;

    // Valid only when reliable == true.
    double frequencyHz = 0.0;     // settled body fundamental
    double confidence = 0.0;      // mean YIN confidence over the frames used
    double midiNote = 0.0;        // fractional MIDI note number
    int    nearestMidi = 0;
    double centsFromNearest = 0.0;
    double firstVoicedHz = 0.0;    // first frame stable enough to trust (NOT the true sweep start)
    double sweepCents = 0.0;       // 1200*log2(max/min) over voiced frames; a lower bound on the real sweep

    int voicedFrames = 0;
    int gatedFrames = 0;
    int totalFrames = 0;

    std::vector<PitchFrame> track; // always filled when analysis ran far enough to make frames
};

class KickPitchAnalyzer
{
public:
    struct Settings
    {
        double minHz = 28.0;
        double maxHz = 400.0;
        float  yinThreshold = 0.2f;
        double hopSec = 0.010;
        double maxAnalysisSec = 0.8;   // how much audio after the peak to look at
        double energyGateDb = -35.0;   // frames this far below the loudest frame are ignored
        int    minVoicedFrames = 3;
        double minVoicedFraction = 0.4; // of gated frames
        double maxSettledSpreadCents = 150.0;
    };

    KickPitchAnalyzer() = default;
    explicit KickPitchAnalyzer(const Settings& s) : settings_(s) {}

    // `mono` is a single channel (sum stereo before calling). Allocates; not for the audio thread.
    KickPitchResult analyze(const float* mono, std::size_t numSamples, double sampleRate) const
    {
        KickPitchResult res;

        if (mono == nullptr || numSamples == 0 || !(sampleRate >= 8000.0) || !std::isfinite(sampleRate))
        {
            res.status = PitchStatus::InvalidInput;
            return res;
        }

        // Peak + validity scan.
        float peak = 0.0f;
        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const float v = mono[i];
            if (!std::isfinite(v))
            {
                res.status = PitchStatus::InvalidInput;
                return res;
            }
            peak = std::max(peak, std::fabs(v));
        }
        if (peak < 1.0e-4f)
        {
            res.status = PitchStatus::Silent;
            return res;
        }

        // Anchor at the onset, not the global peak: a late peak (swelling sub, loud tail,
        // noise) would otherwise leave too little audio after it and skew the analysis.
        std::size_t peakIdx = 0; // (name kept: start of the analysed region)
        while (peakIdx < numSamples && std::fabs(mono[peakIdx]) < 0.1f * peak)
            ++peakIdx;

        const std::size_t maxLen = static_cast<std::size_t>(settings_.maxAnalysisSec * sampleRate);
        const std::size_t len = std::min(numSamples - peakIdx, maxLen);

        const int factor = Decimator::chooseFactor(sampleRate);
        const std::vector<float> dec = Decimator::process(mono + peakIdx, len, factor, sampleRate);
        const double decRate = sampleRate / factor;

        YinEstimator yin;
        yin.prepare(decRate, settings_.minHz, settings_.maxHz, settings_.yinThreshold);
        const std::size_t frameSize = static_cast<std::size_t>(yin.frameSize());

        if (dec.size() < frameSize)
        {
            res.status = PitchStatus::TooShort;
            return res;
        }

        const std::size_t hop = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(settings_.hopSec * decRate)));

        // Frame loop.
        float maxRms = 0.0f;
        for (std::size_t pos = 0; pos + frameSize <= dec.size(); pos += hop)
        {
            PitchFrame f;
            double e = 0.0;
            for (std::size_t i = 0; i < frameSize; ++i) e += static_cast<double>(dec[pos + i]) * dec[pos + i];
            const float rms = static_cast<float>(std::sqrt(e / static_cast<double>(frameSize)));
            maxRms = std::max(maxRms, rms);
            f.rmsDb = 20.0f * std::log10(std::max(rms, 1.0e-10f));

            const YinResult y = yin.process(dec.data() + pos);
            f.frequencyHz = y.frequencyHz;
            f.confidence = y.confidence;
            f.voiced = y.voiced; // gating applied below once maxRms is known
            const double centreOrig = static_cast<double>(peakIdx) +
                                      (static_cast<double>(pos) + 0.5 * static_cast<double>(frameSize)) * factor;
            f.timeSec = centreOrig / sampleRate;
            res.track.push_back(f);
        }
        res.totalFrames = static_cast<int>(res.track.size());

        const float maxRmsDb = 20.0f * std::log10(std::max(maxRms, 1.0e-10f));
        const float gateDb = maxRmsDb + static_cast<float>(settings_.energyGateDb);
        std::vector<double> voicedFreqs;
        for (auto& f : res.track)
        {
            f.gated = f.rmsDb >= gateDb;
            f.voiced = f.voiced && f.gated;
            if (f.gated) ++res.gatedFrames;
            if (f.voiced) { ++res.voicedFrames; voicedFreqs.push_back(f.frequencyHz); }
        }

        if (res.gatedFrames == 0)
        {
            res.status = PitchStatus::Silent;
            return res;
        }
        if (res.voicedFrames < settings_.minVoicedFrames ||
            static_cast<double>(res.voicedFrames) < settings_.minVoicedFraction * res.gatedFrames)
        {
            res.status = PitchStatus::NoPitchFound;
            return res;
        }

        // Settled region = later half of the voiced frames.
        const std::size_t half = voicedFreqs.size() / 2;
        std::vector<double> settled(voicedFreqs.begin() + static_cast<std::ptrdiff_t>(half), voicedFreqs.end());
        const auto [minS, maxS] = std::minmax_element(settled.begin(), settled.end());
        if (1200.0 * std::log2(*maxS / *minS) > settings_.maxSettledSpreadCents)
        {
            res.status = PitchStatus::Unstable;
            return res;
        }

        const auto mid = settled.begin() + static_cast<std::ptrdiff_t>(settled.size() / 2);
        std::nth_element(settled.begin(), mid, settled.end());
        res.frequencyHz = *mid;

        double confSum = 0.0; int confN = 0;
        {
            int seen = 0;
            for (const auto& f : res.track)
            {
                if (!f.voiced) continue;
                if (static_cast<std::size_t>(seen++) >= half) { confSum += f.confidence; ++confN; }
            }
        }
        res.confidence = confN > 0 ? confSum / confN : 0.0;

        const auto [minA, maxA] = std::minmax_element(voicedFreqs.begin(), voicedFreqs.end());
        res.sweepCents = 1200.0 * std::log2(*maxA / *minA);
        res.firstVoicedHz = voicedFreqs.front();

        res.midiNote = hzToMidi(res.frequencyHz);
        const NoteInfo n = nearestNote(res.frequencyHz);
        res.nearestMidi = n.midiNote;
        res.centsFromNearest = n.centsOffset;

        res.reliable = true;
        res.status = PitchStatus::Ok;
        return res;
    }

private:
    Settings settings_;
};

} // namespace atlas::dsp

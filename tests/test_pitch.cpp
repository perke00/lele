#include "TestHarness.h"
#include "TestSignals.h"

#include "../src/dsp/Decimator.h"
#include "../src/dsp/KickPitchAnalyzer.h"
#include "../src/dsp/NoteUtils.h"
#include "../src/dsp/YinEstimator.h"

#include <limits>

using namespace atlas;
using namespace atlas::dsp;
using namespace atlas::test;

// ---------------------------------------------------------------- note utilities

ATLAS_TEST(note_a4_is_440)
{
    CHECK_NEAR(hzToMidi(440.0), 69.0, 1e-9);
    CHECK_NEAR(midiToHz(69.0), 440.0, 1e-9);
    const auto n = nearestNote(440.0);
    CHECK(n.midiNote == 69);
    CHECK_NEAR(n.centsOffset, 0.0, 1e-6);
    CHECK(midiNoteName(69) == "A4");
}

ATLAS_TEST(note_low_kick_notes)
{
    CHECK(midiNoteName(nearestNote(55.0).midiNote) == "A1");
    CHECK(midiNoteName(nearestNote(41.2034).midiNote) == "E1");
    CHECK(midiNoteName(nearestNote(261.6256).midiNote) == "C4");
    CHECK(midiNoteName(-5) == "--");
}

ATLAS_TEST(note_cents_offset_sign)
{
    const double sharp = 440.0 * std::pow(2.0, 20.0 / 1200.0); // +20 cents
    const double flat  = 440.0 * std::pow(2.0, -30.0 / 1200.0);
    CHECK_NEAR(nearestNote(sharp).centsOffset, 20.0, 1e-6);
    CHECK_NEAR(nearestNote(flat).centsOffset, -30.0, 1e-6);
}

// ---------------------------------------------------------------- decimator

ATLAS_TEST(decimator_preserves_low_tone_and_kills_aliasing)
{
    const double sr = 44100.0;
    const int factor = Decimator::chooseFactor(sr);
    // 60 Hz must pass at ~unity gain; 15 kHz would alias and must be strongly attenuated.
    auto low  = makeSine(sr, 60.0, 0.5, 1.0f);
    auto high = makeSine(sr, 15000.0, 0.5, 1.0f);
    auto dl = Decimator::process(low.data(), low.size(), factor, sr);
    auto dh = Decimator::process(high.data(), high.size(), factor, sr);

    auto rmsMid = [](const std::vector<float>& v) {
        double e = 0; std::size_t a = v.size() / 4, b = 3 * v.size() / 4;
        for (std::size_t i = a; i < b; ++i) e += double(v[i]) * v[i];
        return std::sqrt(e / double(b - a));
    };
    const double gainLow = rmsMid(dl) / (1.0 / std::sqrt(2.0));
    const double rejectHighDb = 20.0 * std::log10(rmsMid(dh) / (1.0 / std::sqrt(2.0)) + 1e-12);
    INFO("factor=%d  60Hz gain=%.4f  15kHz residual=%.1f dB", factor, gainLow, rejectHighDb);
    CHECK_NEAR(gainLow, 1.0, 0.01);
    CHECK(rejectHighDb < -60.0);
}

// ---------------------------------------------------------------- YIN on pure tones

ATLAS_TEST(yin_pure_tones_accuracy_across_sample_rates)
{
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const double freqs[] = { 35.0, 45.0, 55.0, 80.0, 110.0, 150.0, 220.0 };
    double worst = 0.0;
    for (double sr : rates)
    {
        const int factor = Decimator::chooseFactor(sr);
        for (double f : freqs)
        {
            auto s = makeSine(sr, f, 0.6, 0.8f);
            auto d = Decimator::process(s.data(), s.size(), factor, sr);
            YinEstimator yin;
            yin.prepare(sr / factor, 28.0, 400.0);
            CHECK(static_cast<int>(d.size()) >= yin.frameSize());
            const YinResult r = yin.process(d.data() + 200);
            const double err = centsBetween(r.frequencyHz, f);
            worst = std::fmax(worst, std::fabs(err));
            CHECK(r.voiced);
            CHECK(r.confidence > 0.95f);
            CHECK_NEAR(err, 0.0, 10.0);
        }
    }
    INFO("worst pure-tone error: %.2f cents", worst);
}

// ---------------------------------------------------------------- analyzer on kicks

ATLAS_TEST(analyzer_clean_swept_kick_finds_settled_pitch)
{
    double worst = 0.0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (double endHz : { 41.2034, 49.0, 55.0, 61.7354 })
        {
            KickParams p; p.sr = sr; p.startHz = 220.0; p.endHz = endHz;
            auto k = makeKick(p);
            KickPitchAnalyzer a;
            const auto r = a.analyze(k.data(), k.size(), sr);
            CHECK(r.reliable);
            const double err = centsBetween(r.frequencyHz, endHz);
            worst = std::fmax(worst, std::fabs(err));
            CHECK_NEAR(err, 0.0, 25.0);
            // Sweep is smeared by the ~70 ms frame (documented limitation) but still visible
            // as a drop from the first trustworthy frame to the settled pitch.
            CHECK(r.firstVoicedHz > r.frequencyHz * 1.05);
            CHECK(r.sweepCents > 50.0);
        }
    }
    INFO("worst swept-kick settled-pitch error: %.2f cents", worst);
}

ATLAS_TEST(analyzer_reports_note_and_cents)
{
    KickParams p; p.endHz = 55.0; // exactly A1
    auto k = makeKick(p);
    const auto r = KickPitchAnalyzer().analyze(k.data(), k.size(), p.sr);
    CHECK(r.reliable);
    CHECK(midiNoteName(r.nearestMidi) == "A1");
    CHECK(std::fabs(r.centsFromNearest) < 25.0);
    CHECK(r.confidence > 0.8);
}

ATLAS_TEST(analyzer_distorted_kick)
{
    // Heavy tanh distortion adds odd harmonics but the fundamental period is unchanged.
    for (double drive : { 3.0, 8.0 })
    {
        KickParams p; p.endHz = 50.0; p.drive = drive; p.ampTauSec = 0.3;
        auto k = makeKick(p);
        const auto r = KickPitchAnalyzer().analyze(k.data(), k.size(), p.sr);
        INFO("drive %.0f: reliable=%d f=%.2f Hz (true 50) status='%s'", drive, int(r.reliable), r.frequencyHz, toString(r.status));
        CHECK(r.reliable);
        if (r.reliable) CHECK_NEAR(centsBetween(r.frequencyHz, 50.0), 0.0, 40.0);
    }
}

ATLAS_TEST(analyzer_kick_with_click_and_noise)
{
    KickParams p; p.endHz = 55.0; p.click = 0.8; p.noise = 0.05;
    auto k = makeKick(p);
    const auto r = KickPitchAnalyzer().analyze(k.data(), k.size(), p.sr);
    INFO("click+noise: reliable=%d f=%.2f Hz status='%s'", int(r.reliable), r.frequencyHz, toString(r.status));
    CHECK(r.reliable);
    if (r.reliable) CHECK_NEAR(centsBetween(r.frequencyHz, 55.0), 0.0, 40.0);
}

ATLAS_TEST(analyzer_kick_with_strong_second_harmonic)
{
    KickParams p; p.endHz = 50.0; p.harmonic2 = 0.7;
    auto k = makeKick(p);
    const auto r = KickPitchAnalyzer().analyze(k.data(), k.size(), p.sr);
    INFO("strong 2nd harmonic: reliable=%d f=%.2f Hz (true 50)", int(r.reliable), r.frequencyHz);
    CHECK(r.reliable);
    if (r.reliable) CHECK_NEAR(centsBetween(r.frequencyHz, 50.0), 0.0, 40.0);
}

// ---------------------------------------------------------------- honesty: refuse to guess

ATLAS_TEST(analyzer_white_noise_is_not_reliable)
{
    auto n = makeNoise(44100.0, 0.6, 0.5f, 99);
    const auto r = KickPitchAnalyzer().analyze(n.data(), n.size(), 44100.0);
    INFO("noise: reliable=%d status='%s'", int(r.reliable), toString(r.status));
    CHECK(!r.reliable);
    CHECK(r.frequencyHz == 0.0);
    CHECK(r.status == PitchStatus::NoPitchFound); // rejected because it is unpitched, not for an incidental reason
}

ATLAS_TEST(analyzer_noise_burst_with_exponential_decay_is_not_reliable)
{
    // Snare-like: decaying noise only. Many seeds so one lucky result can't pass the test.
    int falsePositives = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed)
    {
        auto n = makeNoise(44100.0, 0.6, 0.9f, seed);
        for (std::size_t i = 0; i < n.size(); ++i) n[i] *= static_cast<float>(std::exp(-double(i) / 44100.0 / 0.1));
        if (KickPitchAnalyzer().analyze(n.data(), n.size(), 44100.0).reliable) ++falsePositives;
    }
    INFO("decaying-noise false positives: %d / 20", falsePositives);
    CHECK(falsePositives == 0);
}

ATLAS_TEST(analyzer_edge_cases)
{
    KickPitchAnalyzer a;
    const float one = 0.5f;

    CHECK(a.analyze(nullptr, 100, 44100.0).status == PitchStatus::InvalidInput);
    CHECK(a.analyze(&one, 0, 44100.0).status == PitchStatus::InvalidInput);
    CHECK(a.analyze(&one, 1, 0.0).status == PitchStatus::InvalidInput);
    CHECK(a.analyze(&one, 1, std::numeric_limits<double>::quiet_NaN()).status == PitchStatus::InvalidInput);

    std::vector<float> silence(44100, 0.0f);
    CHECK(a.analyze(silence.data(), silence.size(), 44100.0).status == PitchStatus::Silent);

    std::vector<float> quiet(44100, 0.0f);
    quiet[100] = 1e-6f;
    CHECK(a.analyze(quiet.data(), quiet.size(), 44100.0).status == PitchStatus::Silent);

    std::vector<float> nan = makeSine(44100.0, 55.0, 0.5);
    nan[1000] = std::numeric_limits<float>::quiet_NaN();
    CHECK(a.analyze(nan.data(), nan.size(), 44100.0).status == PitchStatus::InvalidInput);

    std::vector<float> inf = makeSine(44100.0, 55.0, 0.5);
    inf[5] = std::numeric_limits<float>::infinity();
    CHECK(a.analyze(inf.data(), inf.size(), 44100.0).status == PitchStatus::InvalidInput);

    // 5 ms click: far shorter than one analysis frame.
    std::vector<float> click(static_cast<std::size_t>(0.005 * 44100.0));
    for (std::size_t i = 0; i < click.size(); ++i) click[i] = float(std::exp(-double(i) / 40.0));
    const auto rc = a.analyze(click.data(), click.size(), 44100.0);
    CHECK(!rc.reliable);
    CHECK(rc.status == PitchStatus::TooShort);

    // Single-sample buffer.
    CHECK(!a.analyze(&one, 1, 44100.0).reliable);
}

ATLAS_TEST(analyzer_dc_offset_does_not_fake_a_pitch)
{
    std::vector<float> dc(44100, 0.6f);
    const auto r = KickPitchAnalyzer().analyze(dc.data(), dc.size(), 44100.0);
    CHECK(!r.reliable);
}

ATLAS_TEST(analyzer_polarity_and_gain_invariance)
{
    KickParams p; p.endHz = 55.0;
    auto k = makeKick(p);
    auto inv = k; for (auto& s : inv) s = -s;
    auto quiet = k; for (auto& s : quiet) s *= 0.01f; // -40 dB
    const auto r0 = KickPitchAnalyzer().analyze(k.data(), k.size(), p.sr);
    const auto r1 = KickPitchAnalyzer().analyze(inv.data(), inv.size(), p.sr);
    const auto r2 = KickPitchAnalyzer().analyze(quiet.data(), quiet.size(), p.sr);
    CHECK(r0.reliable && r1.reliable && r2.reliable);
    CHECK_NEAR(r1.frequencyHz, r0.frequencyHz, 0.5);
    CHECK_NEAR(r2.frequencyHz, r0.frequencyHz, 0.5);
}

ATLAS_TEST(analyzer_track_is_exposed_for_gui)
{
    KickParams p; p.endHz = 55.0;
    auto k = makeKick(p);
    const auto r = KickPitchAnalyzer().analyze(k.data(), k.size(), p.sr);
    CHECK(r.totalFrames > 10);
    CHECK(static_cast<int>(r.track.size()) == r.totalFrames);
    for (std::size_t i = 1; i < r.track.size(); ++i)
        CHECK(r.track[i].timeSec > r.track[i - 1].timeSec); // strictly increasing time axis
    CHECK(r.voicedFrames <= r.gatedFrames && r.gatedFrames <= r.totalFrames);
}

ATLAS_TEST(analyzer_leading_silence_and_late_peak)
{
    // 120 ms of silence, a quiet click, then a body that swells to its peak ~40 ms later.
    const double sr = 44100.0;
    auto body = makeSine(sr, 50.0, 0.6, 1.0f);
    for (std::size_t i = 0; i < body.size(); ++i)
    {
        const double t = double(i) / sr;
        const double env = (t < 0.04 ? t / 0.04 : 1.0) * std::exp(-t / 0.3);
        body[i] = static_cast<float>(body[i] * env);
    }
    std::vector<float> s(static_cast<std::size_t>(0.12 * sr), 0.0f);
    s.insert(s.end(), body.begin(), body.end());
    const auto r = KickPitchAnalyzer().analyze(s.data(), s.size(), sr);
    INFO("late-swell kick: reliable=%d f=%.2f Hz (true 50)", int(r.reliable), r.frequencyHz);
    CHECK(r.reliable);
    if (r.reliable) CHECK_NEAR(centsBetween(r.frequencyHz, 50.0), 0.0, 20.0);
}

#pragma once
// ATLAS DSP core - note / frequency helpers.
// Convention: MIDI 69 = A4 = 440 Hz, MIDI 60 = C4 (scientific pitch notation).

#include <cmath>
#include <string>

namespace atlas::dsp {

inline double hzToMidi(double hz) noexcept
{
    return 69.0 + 12.0 * std::log2(hz / 440.0);
}

inline double midiToHz(double midi) noexcept
{
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

struct NoteInfo
{
    int    midiNote = 0;      // nearest equal-tempered note
    double centsOffset = 0.0; // signed distance from that note, in (-50, +50]
};

inline NoteInfo nearestNote(double hz) noexcept
{
    NoteInfo n;
    const double midi = hzToMidi(hz);
    n.midiNote = static_cast<int>(std::lround(midi));
    n.centsOffset = (midi - n.midiNote) * 100.0;
    return n;
}

// Non-realtime (allocates). For GUI use.
inline std::string midiNoteName(int midi)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B" };
    if (midi < 0 || midi > 127)
        return "--";
    return std::string(names[midi % 12]) + std::to_string(midi / 12 - 1);
}

} // namespace atlas::dsp

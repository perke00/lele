# ATLAS - Build Status

## Milestone 1: Foundation + pitch detection  (DONE, tested in cloud)

| Component | File | State |
|---|---|---|
| Note/frequency utilities | `src/dsp/NoteUtils.h` | tested |
| FIR low-pass + decimator | `src/dsp/Decimator.h` | tested |
| YIN estimator (RT-safe `process()`) | `src/dsp/YinEstimator.h` | tested, 0 allocations verified |
| Kick pitch analyzer + confidence | `src/dsp/KickPitchAnalyzer.h` | tested |
| Test harness + synthetic kick generator | `tests/` | - |

Run: `make test` (strict warnings, -Werror) and `make sanitize` (ASan + UBSan).

### Known limitations (documented, not hidden)
- Frame length is ~70 ms at the 28 Hz lower bound, so very fast pitch sweeps (<~30 ms) are smeared.
  Those frames are excluded as unvoiced. `firstVoicedHz` is NOT the true sweep start.
- Tested only on synthetic kicks with known ground truth. Real kick samples are untested.
- Pitch *detection* only. Pitch *shifting* is milestone 2, next.
- CMake build + CPack installer verified on Linux (TGZ). Windows NSIS setup is configured but not run in the sandbox.
- A minimal JUCE VST3 (pass-through effect, Arm button, shows detected note) is wired in CMake. Not compiled or loaded in a host in the sandbox: first verification is the Windows CI build + FL Studio.

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>

// ATLAS plugin (milestone 1 build): audio passes through untouched. When armed, the processor
// waits for the first loud transient, records ~1 s of the mono sum, and the editor analyses it
// with the DSP core on the message thread. The audio thread never allocates or locks.
class AtlasProcessor final : public juce::AudioProcessor
{
public:
    enum State : int { Idle = 0, Armed = 1, Capturing = 2, Ready = 3 };

    AtlasProcessor();
    ~AtlasProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ATLAS"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // --- control / readout (message thread) ---
    void arm() noexcept { state_.store(Armed, std::memory_order_release); }
    int  getState() const noexcept { return state_.load(std::memory_order_acquire); }
    double getSampleRate() const noexcept { return sampleRate_; }
    // Only valid while getState() == Ready. Call finishAnalysis() afterwards.
    const std::vector<float>& capturedAudio() const noexcept { return capture_; }
    int capturedLength() const noexcept { return captureLen_; }
    void finishAnalysis() noexcept { state_.store(Idle, std::memory_order_release); }

private:
    std::atomic<int> state_{ Idle };
    std::vector<float> capture_;
    int captureLen_ = 0;
    int writeIdx_ = 0;
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtlasProcessor)
};

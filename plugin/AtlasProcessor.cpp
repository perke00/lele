#include "AtlasProcessor.h"
#include "AtlasEditor.h"
#include <cmath>

AtlasProcessor::AtlasProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void AtlasProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    state_.store(Idle, std::memory_order_release);
    captureLen_ = static_cast<int>(sampleRate * 1.0);
    capture_.assign(static_cast<size_t>(captureLen_), 0.0f);
    writeIdx_ = 0;
}

bool AtlasProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == l.getMainInputChannelSet();
}

void AtlasProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int s = state_.load(std::memory_order_acquire);
    if (s != Armed && s != Capturing)
        return; // pass-through, nothing to do

    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (numCh == 0 || capture_.empty())
        return;

    int cur = s;
    for (int i = 0; i < n; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < numCh; ++c)
            sum += buffer.getReadPointer(c)[i];
        const float mono = sum / static_cast<float>(numCh);

        if (cur == Armed)
        {
            if (std::abs(mono) < 0.05f) // about -26 dBFS trigger
                continue;
            cur = Capturing;
            writeIdx_ = 0;
            state_.store(Capturing, std::memory_order_release);
        }

        capture_[static_cast<size_t>(writeIdx_++)] = mono;
        if (writeIdx_ >= captureLen_)
        {
            state_.store(Ready, std::memory_order_release);
            return;
        }
    }
}

juce::AudioProcessorEditor* AtlasProcessor::createEditor() { return new AtlasEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AtlasProcessor(); }

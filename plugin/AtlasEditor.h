#pragma once
#include "AtlasProcessor.h"

class AtlasEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AtlasEditor(AtlasProcessor&);
    ~AtlasEditor() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void analyse();

    AtlasProcessor& proc_;
    juce::TextButton armButton_{ "Arm - then play your kick" };
    juce::Label noteLabel_, detailLabel_, statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtlasEditor)
};

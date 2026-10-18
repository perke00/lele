#include "AtlasEditor.h"
#include "dsp/KickPitchAnalyzer.h"

AtlasEditor::AtlasEditor(AtlasProcessor& p) : juce::AudioProcessorEditor(&p), proc_(p)
{
    setSize(420, 260);

    armButton_.onClick = [this]
    {
        proc_.arm();
        statusLabel_.setText("Armed - waiting for a kick...", juce::dontSendNotification);
    };
    addAndMakeVisible(armButton_);

    noteLabel_.setFont(juce::FontOptions(56.0f, juce::Font::bold));
    noteLabel_.setJustificationType(juce::Justification::centred);
    noteLabel_.setText("--", juce::dontSendNotification);
    addAndMakeVisible(noteLabel_);

    detailLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(detailLabel_);

    statusLabel_.setJustificationType(juce::Justification::centred);
    statusLabel_.setText("Press Arm, then play a kick through this plugin.", juce::dontSendNotification);
    addAndMakeVisible(statusLabel_);

    startTimerHz(20);
}

void AtlasEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff14161a));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("ATLAS - Kick Pitch Detector", 0, 8, getWidth(), 28, juce::Justification::centred);
}

void AtlasEditor::resized()
{
    auto r = getLocalBounds().reduced(16);
    r.removeFromTop(32);
    armButton_.setBounds(r.removeFromTop(36));
    r.removeFromTop(8);
    noteLabel_.setBounds(r.removeFromTop(76));
    detailLabel_.setBounds(r.removeFromTop(28));
    statusLabel_.setBounds(r.removeFromTop(40));
}

void AtlasEditor::timerCallback()
{
    if (proc_.getState() == AtlasProcessor::Ready)
        analyse();
}

void AtlasEditor::analyse()
{
    using namespace atlas::dsp;
    const KickPitchAnalyzer analyzer;
    const auto res = analyzer.analyze(proc_.capturedAudio().data(),
                                      static_cast<std::size_t>(proc_.capturedLength()),
                                      proc_.getSampleRate());
    proc_.finishAnalysis();

    if (res.reliable)
    {
        noteLabel_.setText(juce::String(midiNoteName(res.nearestMidi)), juce::dontSendNotification);
        detailLabel_.setText(juce::String::formatted("%.1f Hz   %+.0f cents   confidence %.0f%%",
                                                     res.frequencyHz, res.centsFromNearest,
                                                     res.confidence * 100.0),
                             juce::dontSendNotification);
        statusLabel_.setText("Done. Press Arm to analyse another kick.", juce::dontSendNotification);
    }
    else
    {
        noteLabel_.setText("--", juce::dontSendNotification);
        detailLabel_.setText("", juce::dontSendNotification);
        statusLabel_.setText(juce::String(toString(res.status)), juce::dontSendNotification);
    }
}

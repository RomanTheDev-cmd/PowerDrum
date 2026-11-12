#pragma once
#include "PluginProcessor.h"
// Temporary editor (the real one is being written).
class PowerDrumEditor : public juce::GenericAudioProcessorEditor
{
public:
    explicit PowerDrumEditor (PowerDrumProcessor& p) : juce::GenericAudioProcessorEditor (p) {}
};

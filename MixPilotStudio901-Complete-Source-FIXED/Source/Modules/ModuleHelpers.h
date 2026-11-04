#pragma once
// Header-only math helpers shared by independently compiled DSP modules.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
namespace mixpilot::module {
inline float db(float x) noexcept { return juce::Decibels::gainToDecibels(juce::jmax(x,1.0e-9f)); }
inline float amp(float x) noexcept { return juce::Decibels::decibelsToGain(x); }
}

#include "../PluginProcessor.h"
#include "ModuleHelpers.h"
#include "../SaturationMath.h"
#include <cmath>
#include <algorithm>
using mixpilot::module::db;
using mixpilot::module::amp;

float MixPilotProcessor::processFET76(float x, const Settings& cfg, StageState& st, const ModuleFrame& frame) {
    // Fast peak-detecting FET-style compressor.
                            const float level=std::abs(x);
                            const float cf=level>st.envelope?cfg.attackCoef:cfg.releaseCoef;
                            st.envelope=cf*st.envelope+(1.0f-cf)*level;
                            const float threshold=-6.0f-42.0f*cfg.amount;
                            const float detected=db(st.envelope);
                            const float over=juce::jmax(0.0f,detected-threshold);
                            const float targetDb=-over*(1.0f-1.0f/cfg.ratio);
                            const float target=amp(targetDb);
                            // Smooth gain at audio rate using the configured envelope time.
                            const float smooth=target<st.compGain?cfg.attackCoef:cfg.releaseCoef;
                            st.compGain=smooth*st.compGain+(1.0f-smooth)*target;
                            x*=st.compGain;
                            const float applied=juce::jmax(0.0f,-db(st.compGain));
                            frame.gainReduction=juce::jmax(frame.gainReduction,applied);
                            frame.compressorGainReduction=juce::jmax(frame.compressorGainReduction,applied);
                            frame.compressorDetectorDb=juce::jmax(frame.compressorDetectorDb,detected);
    return x;
}

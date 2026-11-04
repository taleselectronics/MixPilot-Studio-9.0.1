#include "../PluginProcessor.h"
#include "ModuleHelpers.h"
#include "../SaturationMath.h"
#include <cmath>
#include <algorithm>
using mixpilot::module::db;
using mixpilot::module::amp;

float MixPilotProcessor::processOptoLeveler(float x, const Settings& cfg, StageState& st, const ModuleFrame& frame) {
    // RMS-style, program-dependent optical leveling.
                            // Not a circuit-level LA-2A emulation: distinct level detector
                            // and slower, level-dependent release are intentional.
                            const float power=x*x;
                            const float attack=std::exp(-1.0f/(float)(sampleRate*0.010));
                            const float releaseSec=0.12f+0.65f*juce::jlimit(0.0f,1.0f,1.0f-st.compGain);
                            const float release=std::exp(-1.0f/(float)(sampleRate*releaseSec));
                            const float cf=power>st.envelope?attack:release;
                            st.envelope=cf*st.envelope+(1.0f-cf)*power;
                            const float detected=db(std::sqrt(juce::jmax(0.0f,st.envelope)));
                            const float threshold=-9.0f-33.0f*cfg.amount;
                            const float over=juce::jmax(0.0f,detected-threshold);
                            const float ratio=juce::jlimit(1.5f,5.0f,cfg.ratio);
                            const float target=amp(-over*(1.0f-1.0f/ratio));
                            const float gainCoeff=target<st.compGain?attack:release;
                            st.compGain=gainCoeff*st.compGain+(1.0f-gainCoeff)*target;
                            x*=st.compGain;
                            const float applied=juce::jmax(0.0f,-db(st.compGain));
                            frame.gainReduction=juce::jmax(frame.gainReduction,applied);
                            frame.compressorGainReduction=juce::jmax(frame.compressorGainReduction,applied);
                            frame.compressorDetectorDb=juce::jmax(frame.compressorDetectorDb,detected);
    return x;
}

#include "../PluginProcessor.h"
#include "ModuleHelpers.h"
#include "../SaturationMath.h"
#include <cmath>
#include <algorithm>
using mixpilot::module::db;
using mixpilot::module::amp;

float MixPilotProcessor::processDelay(float x, const Settings& cfg, StageState& st, const ModuleFrame& frame) {
    // Different feedback topology for stereo, cross-fed ping-pong
                            // and gently darkened tape-style echo. No allocations.
                            const float delayed=st.echoDelay.read((float)cfg.delaySamples);
                            const float duckEnvelope=std::abs(frame.stageInput);
                            const float duckCoef=std::exp(-1.0f/(float)(sampleRate*.065));
                            st.delayDuckEnv=juce::jmax(duckEnvelope,st.delayDuckEnv*duckCoef);
                            const float duckGain=1.0f-cfg.delayDuck*juce::jlimit(0.0f,1.0f,st.delayDuckEnv*4.0f);
                            const float feedbackSource=cfg.delayMode==1&&frame.channels==2
                                ?frame.oppositeChannel->delayLast:delayed;
                            st.delayToneState=cfg.delayToneCoef*st.delayToneState+
                                (1.0f-cfg.delayToneCoef)*feedbackSource;
                            const float feedbackSignal=cfg.delayMode==2
                                ?std::tanh(st.delayToneState*1.3f)/1.3f:st.delayToneState;
                            st.echoDelay.write(x+feedbackSignal*cfg.delayFeedback);
                            st.delayLast=delayed;
                            x=delayed*duckGain;
    return x;
}

#include "../PluginProcessor.h"
#include "ModuleHelpers.h"
#include "../SaturationMath.h"
#include <cmath>
#include <algorithm>
using mixpilot::module::db;
using mixpilot::module::amp;

float MixPilotProcessor::processReverb(float x, const Settings& cfg, StageState& st, const ModuleFrame& frame) {
    // Three decorrelated delay paths with mode-dependent lengths.
                            // Exact room/tape hardware modeling is not claimed.
                            const auto& pre=st.predelay;
                            const size_t preSize=pre.size();
                            const size_t preRead=(st.predelayWrite+preSize-
                                juce::jmin(cfg.spacePreSamples,preSize-1))%preSize;
                            const float early=cfg.spacePreSamples==0?x:st.predelay[preRead];
                            st.predelay[st.predelayWrite]=x;
                            st.predelayWrite=(st.predelayWrite+1)%preSize;
                            float combined=0.0f;
                            for(int t=0;t<3;++t){
                                auto& line=st.reverbDelay[(size_t)t];
                                const size_t length=line.size();
                                const size_t tapLength=cfg.spaceTapSamples[(size_t)t];
                                const size_t pos=st.reverbPos[(size_t)t];
                                const size_t read=(pos+length-tapLength)%length;
                                const float tap=line[read];
                                // The T60 coefficient is computed once per block.
                                const float damping=.05f+.90f*cfg.spaceDamp;
                                st.reverbDamp[(size_t)t]=damping*st.reverbDamp[(size_t)t]+(1.0f-damping)*tap;
                                line[pos]=early*.48f+st.reverbDamp[(size_t)t]*cfg.spaceFeedback[(size_t)t];
                                st.reverbPos[(size_t)t]=(pos+1)%length;
                                combined+=tap;
                            }
                            float wet=combined*.23f;
                            // One-pole high-pass and low-pass on the wet path only.
                            st.reverbLow=cfg.spaceLowCoef*st.reverbLow+(1.0f-cfg.spaceLowCoef)*wet;
                            wet-=st.reverbLow;
                            st.reverbHigh=cfg.spaceHighCoef*st.reverbHigh+(1.0f-cfg.spaceHighCoef)*wet;
                            x=st.reverbHigh;
    return x;
}

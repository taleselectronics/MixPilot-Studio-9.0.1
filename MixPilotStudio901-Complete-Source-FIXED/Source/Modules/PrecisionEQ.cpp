#include "../PluginProcessor.h"
#include "ModuleHelpers.h"
#include "../SaturationMath.h"
#include <cmath>
#include <algorithm>
using mixpilot::module::db;
using mixpilot::module::amp;

float MixPilotProcessor::processPrecisionEQ(float x, int type, const Settings& cfg, StageState& st, const ModuleFrame& frame) {
    x=st.filter.processSingleSampleRaw(x);
                            if(type==1){
                                for(int band=0;band<eqBands;++band){
                                    if(band>0)x=st.extraEQ[(size_t)(band-1)].processSingleSampleRaw(x);
                                    const auto& ds=frame.dynamic[(size_t)band];
                                    if(!ds.enabled)continue;
                                    auto& state=st.dynamicEQ[(size_t)band];
                                    const float detection=std::abs(state.detector.processSingleSampleRaw(x));
                                    const float coeff=detection>state.envelope?ds.attackCoef:ds.releaseCoef;
                                    state.envelope=coeff*state.envelope+(1.0f-coeff)*detection;
                                    // A continuous, bounded detector. 18 dB over threshold reaches full range.
                                    const float above=db(state.envelope)-ds.threshold;
                                    const float target=juce::jlimit(0.0f,1.0f,above/18.0f);
                                    state.wet=coeff*state.wet+(1.0f-coeff)*target;
                                    const float filtered=state.colourFilter.processSingleSampleRaw(x);
                                    x=x+(filtered-x)*state.wet;
                                }
                            }
                            else if(cfg.slope==1)x=st.second.processSingleSampleRaw(x);
    return x;
}

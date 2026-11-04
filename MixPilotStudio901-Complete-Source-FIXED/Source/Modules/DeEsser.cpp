#include "../PluginProcessor.h"
#include "ModuleHelpers.h"
#include "../SaturationMath.h"
#include <cmath>
#include <algorithm>
using mixpilot::module::db;
using mixpilot::module::amp;

float MixPilotProcessor::processDeEsser(float x, const Settings& cfg, StageState& st, const ModuleFrame& frame) {
    // Sibilance detector with optional split-band attenuation.
                            // Filter coefficients and time constants update outside the sample loop.
                            const float high=x-st.deEssLow.processSingleSampleRaw(x);
                            const float detected=high*high;
                            const float coeff=detected>st.deEssEnvelope?cfg.attackCoef:cfg.releaseCoef;
                            st.deEssEnvelope=coeff*st.deEssEnvelope+(1.0f-coeff)*detected;
                            const float levelDb=db(std::sqrt(juce::jmax(0.0f,st.deEssEnvelope)));
                            // Gently progressive attenuation, bounded by the max range.
                            const float over=juce::jmax(0.0f,levelDb-cfg.deessThreshold);
                            const float targetDb=juce::jmin(cfg.deessRange,over*0.7f);
                            const float smooth=targetDb>st.deEssGainDb?cfg.attackCoef:cfg.releaseCoef;
                            st.deEssGainDb=smooth*st.deEssGainDb+(1.0f-smooth)*targetDb;
                            const float applied=juce::jlimit(0.0f,18.0f,st.deEssGainDb);
                            const float atten=amp(-applied);
                            x=cfg.deessWide ? x*atten : x-high*(1.0f-atten);
                            frame.gainReduction=juce::jmax(frame.gainReduction,applied);
                            frame.deEsserGainReduction=juce::jmax(frame.deEsserGainReduction,applied);
                            frame.deEsserDetectorDb=juce::jmax(frame.deEsserDetectorDb,levelDb);
    return x;
}

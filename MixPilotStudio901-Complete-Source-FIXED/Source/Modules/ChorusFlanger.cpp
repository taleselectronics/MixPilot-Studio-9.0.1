#include "../PluginProcessor.h"
#include "ModuleHelpers.h"
#include "../SaturationMath.h"
#include <cmath>
#include <algorithm>
using mixpilot::module::db;
using mixpilot::module::amp;

float MixPilotProcessor::processChorusFlanger(float x, int type, const Settings& cfg, StageState& st, const ModuleFrame& frame) {
    // Chorus: longer LFO-modulated dual-voice delay.
                            // Flanger: short comb delay with feedback.
                            auto& line=st.modulationDelay;
                            const auto size=line.size();
                            const float phase=st.modPhase+(frame.channel==1?cfg.modStereo/360.0f:0.0f);
                            const float lfo=std::sin(2.0f*juce::MathConstants<float>::pi*phase);
                            const float baseMs=type==14?17.0f:2.8f;
                            const float travelMs=type==14?11.0f:2.3f;
                            const float samples=(baseMs+travelMs*cfg.modDepth*lfo)*
                                (float)sampleRate*.001f;
                            const float bounded=juce::jlimit(1.0f,(float)size-3.0f,samples);
                            const int whole=(int)bounded;
                            const float fract=bounded-(float)whole;
                            const size_t ix0=(st.modulationWrite+size-(size_t)whole)%size;
                            const size_t ix1=(ix0+size-1)%size;
                            const float tap=juce::jmap(fract,line[ix0],line[ix1]);
                            line[st.modulationWrite]=x+st.modFeedbackSample*cfg.modFeedback;
                            st.modulationWrite=(st.modulationWrite+1)%size;
                            st.modFeedbackSample=tap;
                            st.modPhase+=cfg.modRate/(float)sampleRate;
                            if(st.modPhase>=1.0f)st.modPhase-=1.0f;
                            x=tap;
    return x;
}

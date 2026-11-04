#include "../PluginProcessor.h"
#include "ModuleHelpers.h"
#include "../SaturationMath.h"
#include <cmath>
#include <algorithm>
using mixpilot::module::db;
using mixpilot::module::amp;

float MixPilotProcessor::processSaturation(float x, const Settings& cfg, StageState& st, const ModuleFrame& frame) {
    // Three genuinely distinct bounded waveshaping models.
                            // Tone shaping, state and DC rejection live on the audio thread;
                            // no memory allocation or parameter-tree lookup per sample.
                            const float input=x;
                            const float drive=mixpilot::saturationDrive(cfg.amount);
                            // Drive now changes harmonic density directly. Keep output
                            // makeup bounded; the separate module MIX knob blends dry/wet.
                            const float gainComp=mixpilot::saturationMakeup(drive);
                            const float character=cfg.satBias;
                            const int factor=cfg.satOversample;
                            float aggregate=0.0f;
                            for(int sub=1;sub<=factor;++sub){
                                // Linear interpolation creates new intermediate samples.
                                // Averaging the oversampled outputs provides basic low-pass
                                // decimation. This is NOT a high-rejection FIR oversampler.
                                const float sample=juce::jmap((float)sub/(float)factor,st.satPrevious,input);
                                st.satToneLow=cfg.satToneCoeff*st.satToneLow+(1.0f-cfg.satToneCoeff)*sample;
                                const float high=sample-st.satToneLow;
                                const float toned=st.satToneLow+high*(1.0f+cfg.satTone*.55f);
                                float shaped=toned;
                                switch(cfg.satMode){
                                    case 0:{ // Tube: biased asymmetric transfer + even harmonics.
                                        shaped=mixpilot::tubeCurve(toned,character,drive,gainComp);
                                        break;
                                    }
                                    case 1:{ // Tape: memory-dependent soft-knee + HF saturation.
                                        st.satTapeMemory=cfg.satTapeCoeff*st.satTapeMemory+
                                            (1.0f-cfg.satTapeCoeff)*toned;
                                        shaped=mixpilot::tapeCurve(toned,st.satTapeMemory,character,drive,gainComp);
                                        break;
                                    }
                                    default:{ // Transformer: LF-dependent nonlinearity and bias.
                                        st.satTransformerLow=cfg.satLfCoeff*st.satTransformerLow+
                                            (1.0f-cfg.satLfCoeff)*toned;
                                        shaped=mixpilot::transformerCurve(toned,st.satTransformerLow,character,drive,gainComp);
                                        break;
                                    }
                                }
                                aggregate+=shaped;
                            }
                            st.satPrevious=input;
                            // Zero drive is dry. At nonzero drive, module MIX controls
                            // parallel blending (not the Drive knob twice).
                            const float coloured=aggregate/(float)factor;
                            const float processed=mixpilot::saturationBypass(input,coloured,cfg.amount);
                            // Remove nonlinear DC while maintaining near-unity LF response.
                            const float corrected=processed-st.satDcInput+cfg.satDcCoeff*st.satDcOutput;
                            st.satDcInput=processed;st.satDcOutput=corrected;
                            x=(cfg.amount>0.0001f?corrected:input)*cfg.satTrim;
    return x;
}

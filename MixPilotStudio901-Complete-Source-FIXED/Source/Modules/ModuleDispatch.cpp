#include "../PluginProcessor.h"
#include "ModuleHelpers.h"
using mixpilot::module::amp;
// Only this registry knows the module type-to-DSP-function mapping.
// Existing algorithms can be modified in their own file without touching
// PluginProcessor.cpp; the last stage limiter is handled globally.
float MixPilotProcessor::processModule(float x,int type,const Settings& cfg,
                                       StageState& st,const ModuleFrame& frame){
switch(type){
                    case 1:case 2:case 3:x=processPrecisionEQ(x,type,cfg,st,frame);break;
                    case 4:x=processFET76(x,cfg,st,frame);break;
                    case 13:x=processOptoLeveler(x,cfg,st,frame);break;
                    case 5:x=processDeEsser(x,cfg,st,frame);break;
                    case 6:x=processSaturation(x,cfg,st,frame);break;
                    case 7:x*=amp(cfg.gain);break;
                    case 9:x=processDelay(x,cfg,st,frame);break;
                    case 10:break; // Final lookahead limiter runs once after the rack.
                    case 11:x=processReverb(x,cfg,st,frame);break;
                    case 12:x=processPhaser(x,cfg,st,frame);break;
                    case 14:case 15:x=processChorusFlanger(x,type,cfg,st,frame);break;
                    default:break;
                }
    return x;
}

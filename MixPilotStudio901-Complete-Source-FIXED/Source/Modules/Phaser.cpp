#include "../PluginProcessor.h"
#include "ModuleHelpers.h"
#include "../SaturationMath.h"
#include <cmath>
#include <algorithm>
using mixpilot::module::db;
using mixpilot::module::amp;

float MixPilotProcessor::processPhaser(float x, const Settings& cfg, StageState& st, const ModuleFrame& frame) {
    // DaisySP Phaser. Wet signal comes from its own MIT DSP core.
                            x=st.phaser.Process(x);
    return x;
}

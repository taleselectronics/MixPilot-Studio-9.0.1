#include "../PluginProcessor.h"
void MixPilotProcessor::processStereoWidth(float y[2], const float stageInput[2], const Settings& cfg, int chans) {
    const float amount=cfg.amount, mix=cfg.mix;
    if(chans!=2)return;
    const float width=juce::jmap(amount,0.0f,2.0f);
    const float mid=.5f*(stageInput[0]+stageInput[1]);
    const float side=.5f*(stageInput[0]-stageInput[1])*width;
    y[0]=stageInput[0]*(1-mix)+(mid+side)*mix;
    y[1]=stageInput[1]*(1-mix)+(mid-side)*mix;
}

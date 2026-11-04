#pragma once
// Independently testable, JUCE-free vocal-chain planning data and policy.
// No DSP processing or host calls happen here.
#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cctype>

namespace mixpilot::planning {
struct VocalBand {
    int index=0, filter=0;
    float freq=1000, gain=0, q=1.1f, threshold=-24, range=-4, attack=12, release=120;
    bool dynamic=false;
};
struct VocalModule{
    int type=0;float freq=1200,gain=0,q=1.1f,amount=30,ratio=3,attack=12,release=110,mix=100,slope=0;
    float deessThreshold=-30,deessRange=8,deessMode=0;
    float satMode=0,satQuality=0,satTone=0,satBias=0,satOutput=0;
    float limInput=0,limCeiling=-1,limRelease=140,limTruePeak=1;
    float spaceMode=0,spaceDecay=2,spacePredelay=25,spaceDamp=50,spaceLowCut=160,spaceHighCut=10000;
    float delayMode=0,delayMs=275,delaySync=0,delayDivision=1,delayFeedback=28,delayTone=35,delayDuck=40;
    float modRate=.65f,modDepth=55,modFeedback=0,modStereo=90;
    std::vector<VocalBand> bands;
};

inline float sourceReferenceHighPassHz(const std::array<float,18>& source,
                                      const std::array<float,18>& reference,
                                      bool bothValid) noexcept {
    // Heuristic: below ~90 Hz excess versus the reference. This is NOT
    // pitch detection and must never be presented as an optimal cutoff.
    if(!bothValid)return 70.0f;
    float delta=0.0f;
    for(int i=0;i<4;++i)delta+=(source[(size_t)i]-reference[(size_t)i])*0.25f;
    return std::clamp(65.0f+2.5f*delta,45.0f,105.0f);
}
inline bool referenceShowsStereoSpace(bool stereoValid,float correlation) noexcept {
    // Correlation alone cannot identify delay/reverb. Use as a weak cue
    // to test modest ambience when a reference-style match is requested.
    return stereoValid && std::isfinite(correlation) && correlation<0.975f;
}
struct Options {
    bool vocal=false, minimal=false, dry=false;
    bool referenceMatch=false, hasReferenceSpatialCue=false;
    bool requestDelay=false, requestReverb=false, requestSaturation=false;
};
inline bool hasType(const std::array<VocalModule,10>& chain,int count,int wanted){
    for(int i=0;i<count;++i)if(chain[(size_t)i].type==wanted)return true;
    return false;
}
inline int consolidateCutModules(std::array<VocalModule,10>& chain,int count,
                                 float fallbackCutoff,int& converted) {
    std::vector<VocalBand> cutBands;
    std::array<VocalModule,10> compact{};
    int n=0;
    converted=0;
    for(int i=0;i<count;++i){
        auto m=chain[(size_t)i];
        if(m.type==2||m.type==3){
            VocalBand band;
            band.filter=m.type==2?4:5;
            band.freq=m.type==2?std::clamp(m.freq,35.0f,220.0f):
                                 std::clamp(m.freq,3000.0f,20000.0f);
            band.q=0.7071f;
            cutBands.push_back(band);
            ++converted;
        } else if(n<10)compact[(size_t)n++]=std::move(m);
    }
    if(cutBands.empty()){chain=std::move(compact);return n;}
    int eq=-1;
    for(int i=0;i<n;++i)if(compact[(size_t)i].type==1){eq=i;break;}
    if(eq<0){
        if(n>=10){chain=std::move(compact);return n;}
        for(int j=n;j>0;--j)compact[(size_t)j]=std::move(compact[(size_t)(j-1)]);
        compact[0]=VocalModule{};compact[0].type=1;
        ++n;eq=0;
    }
    for(auto& band:cutBands){
        auto& bands=compact[(size_t)eq].bands;
        if(std::any_of(bands.begin(),bands.end(),[&](const auto& b){return b.filter==band.filter;}))continue;
        std::array<bool,12> taken{};
        for(const auto& b:bands)if(b.index>=1&&b.index<=12)taken[(size_t)(b.index-1)]=true;
        for(int index=12;index>=1;--index){
            if(taken[(size_t)(index-1)])continue;
            band.index=index;
            if(band.filter==4 && band.freq>180.0f)band.freq=fallbackCutoff;
            bands.push_back(band);
            break;
        }
    }
    chain=std::move(compact);
    return n;
}
inline int supplementMeasuredLowCut(std::array<VocalModule,10>& chain,int count,
                                    const std::array<float,18>& source,
                                    const std::array<float,18>& reference,
                                    bool bothValid) {
    // A broadband low-frequency *difference* can support a modest rumble cut,
    // but not automatic removal of the vocal fundamental. Require evidence.
    if(!bothValid)return 0;
    float lowDelta=0.0f;
    for(int i=0;i<4;++i)lowDelta+=(source[(size_t)i]-reference[(size_t)i])*0.25f;
    if(lowDelta<4.0f)return 0;
    for(int slot=0;slot<count;++slot){
        auto& m=chain[(size_t)slot];
        if(m.type!=1)continue;
        for(const auto& b:m.bands)if(b.filter==4)return 0;
        std::array<bool,12> used{};
        for(const auto& b:m.bands)
            if(b.index>=1 && b.index<=12)used[(size_t)(b.index-1)]=true;
        for(int index=12;index>=1;--index){
            if(used[(size_t)(index-1)])continue;
            VocalBand band;band.index=index;band.filter=4;band.q=0.7071f;
            band.freq=sourceReferenceHighPassHz(source,reference,true);
            m.bands.push_back(band);
            return 1;
        }
    }
    return 0;
}
inline int completeVocalPlan(std::array<VocalModule,10>& chain,int count,
                             const Options& opt,int& added){
    added=0;
    if(!opt.vocal||opt.minimal)return count;
    // Music-informed defaults apply only to full vocal-production requests.
    // Don't interpret a stereo signal as proof of specific reverb/delay.
    const bool includeSpace=!opt.dry && (opt.requestReverb ||
        (opt.referenceMatch && opt.hasReferenceSpatialCue));
    const bool includeDelay=!opt.dry && (opt.requestDelay ||
        (opt.referenceMatch && opt.hasReferenceSpatialCue));
    std::array<VocalModule,10> planned{};
    int n=0;
    auto add=[&](int wanted,bool optional){
        if(optional&&!hasType(chain,count,wanted))return;
        if(n>=10)return;
        for(int i=0;i<count;++i){
            if(chain[(size_t)i].type==wanted){planned[(size_t)n++]=chain[(size_t)i];return;}
        }
        VocalModule m;m.type=wanted;
        if(wanted==4){m.amount=32;m.ratio=3;m.attack=12;m.release=110;}
        if(wanted==5){m.freq=6500;m.deessThreshold=-28;m.deessRange=5;m.amount=20;}
        if(wanted==6){m.amount=opt.requestSaturation?60:30;m.satMode=0;m.satQuality=1;m.mix=75;}
        if(wanted==9){m.delayMs=260;m.delayFeedback=20;m.delayDuck=45;m.mix=12;}
        if(wanted==11){m.spaceDecay=1.4f;m.spacePredelay=35;m.mix=14;}
        planned[(size_t)n++]=m;
        ++added;
    };
    add(1,false);
    add(4,false);
    add(13,true);
    add(5,false);
    add(6,!opt.requestSaturation && count>=5);
    add(8,true);
    if(includeDelay)add(9,false);
    else if(!opt.dry)add(9,true);
    if(includeSpace)add(11,false);
    else if(!opt.dry)add(11,true);
    // Keep any AI-requested processors not handled above.
    for(int i=0;i<count&&n<10;++i){
        const int type=chain[(size_t)i].type;
        if(opt.dry && (type==9||type==11))continue;
        if(!hasType(planned,n,type))planned[(size_t)n++]=chain[(size_t)i];
    }
    // Final limiter stays last when recommended by Qwen.
    if(hasType(chain,count,10)){
        for(int i=0;i<n;++i)if(planned[(size_t)i].type==10){
            VocalModule limiter=planned[(size_t)i];
            for(int j=i;j<n-1;++j)planned[(size_t)j]=std::move(planned[(size_t)(j+1)]);
            planned[(size_t)(n-1)]=std::move(limiter);break;
        }
    }
    chain=std::move(planned);
    return n;
}
} // namespace mixpilot::planning

#include "../Source/AI/VocalChainPlanner.h"
#include "../Source/AI/ReferenceMetrics.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace mixpilot::planning;
int main(){
    std::array<float,18> source{},ref{};
    for(int i=0;i<4;++i)source[(size_t)i]=10.0f;
    const float cutoff=sourceReferenceHighPassHz(source,ref,true);
    assert(cutoff>65.0f && cutoff<105.0f);
    assert(sourceReferenceHighPassHz(source,ref,false)==70.0f);
    assert(referenceShowsStereoSpace(true,.91f));
    assert(!referenceShowsStereoSpace(true,.999f));
    assert(!referenceShowsStereoSpace(false,.91f));
    assert(spectralDistanceDb(source,ref)>2.0f);
    assert(spectralDistanceDb(ref,ref)==0.0f);
    std::array<VocalModule,10> chain{};
    chain[0].type=2;chain[0].freq=84;
    chain[1].type=1;
    VocalBand eq;eq.index=1;eq.freq=340;eq.gain=-2.0f;
    chain[1].bands.push_back(eq);
    chain[2].type=4;
    int converted=0;
    int count=consolidateCutModules(chain,3,cutoff,converted);
    assert(count==2 && converted==1);
    assert(chain[0].type==1 && chain[1].type==4);
    assert(chain[0].bands.size()==2);
    assert(chain[0].bands[0].index==1);
    assert(chain[0].bands[1].filter==4);
    assert(chain[0].bands[1].freq==84);
    int lowCutAdded=supplementMeasuredLowCut(chain,count,source,ref,true);
    assert(lowCutAdded==0); // already has HPF after consolidation
    Options opts; opts.vocal=true;opts.referenceMatch=true;opts.hasReferenceSpatialCue=true; opts.requestSaturation=true;
    int added=0;
    count=completeVocalPlan(chain,count,opts,added);
    assert(count>=6);
    assert(hasType(chain,count,1));
    assert(!hasType(chain,count,2));
    assert(hasType(chain,count,4));
    assert(hasType(chain,count,5));
    assert(hasType(chain,count,6));
    assert(hasType(chain,count,9));
    assert(hasType(chain,count,11));
    assert(added>=4);
    std::array<VocalModule,10> fresh{}; fresh[0].type=1;
    int addLow=supplementMeasuredLowCut(fresh,1,source,ref,true);
    assert(addLow==1 && fresh[0].bands.size()==1 && fresh[0].bands[0].filter==4);
    assert(supplementMeasuredLowCut(fresh,1,source,ref,true)==0);
    std::array<VocalModule,10> dry{};
    dry[0].type=1; dry[1].type=4; dry[2].type=9;dry[3].type=11;
    Options dryOpt=opts;dryOpt.dry=true;
    count=completeVocalPlan(dry,4,dryOpt,added);
    assert(!hasType(dry,count,9) && !hasType(dry,count,11));
    std::array<VocalModule,10> minimal{};
    minimal[0].type=1;
    Options minimalOpt=opts;minimalOpt.minimal=true;
    count=completeVocalPlan(minimal,1,minimalOpt,added);
    assert(count==1 && added==0);
    std::cout<<"Vocal-chain, combined-cutoff, reference cue and spectral distance tests passed\n";
}

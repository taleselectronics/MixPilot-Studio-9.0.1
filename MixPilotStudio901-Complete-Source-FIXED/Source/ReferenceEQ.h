#pragma once
// Conservative, independent source/reference regional EQ proposals.
// These are BROAD tonal clues, not proof of an unwanted vocal resonance.
#include <algorithm>
#include <array>
#include <cmath>
namespace mixpilot {
struct EqSuggestion {
    float hz=1000, gainDb=0, q=0.85f;
    int region=-1;
};
struct EqSuggestionSet {
    std::array<EqSuggestion,6> items{};
    int count=0;
};
inline EqSuggestionSet referenceEqSuggestions(const std::array<float,18>& source,
                                             const std::array<float,18>& reference){
    EqSuggestionSet out;
    std::array<float,18> delta{};
    for(int i=0;i<18;++i)delta[(size_t)i]=source[(size_t)i]-reference[(size_t)i];
    std::array<bool,18> used{};
    for(int attempt=0;attempt<6;++attempt){
        int best=-1;
        float bestScore=3.2f;
        for(int b=3;b<16;++b){
            if(used[(size_t)b])continue;
            const float local=0.7f*delta[(size_t)b]+
                0.15f*(delta[(size_t)(b-1)]+delta[(size_t)(b+1)]);
            if(std::abs(local)>bestScore){bestScore=std::abs(local);best=b;}
        }
        if(best<0)break;
        for(int adjacent=std::max(0,best-1);adjacent<=std::min(17,best+1);++adjacent)
            used[(size_t)adjacent]=true;
        EqSuggestion s;
        s.region=best;
        s.hz=30.0f*std::pow(16000.0f/30.0f,((float)best+0.5f)/18.0f);
        const float local=0.7f*delta[(size_t)best]+
            0.15f*(delta[(size_t)(best-1)]+delta[(size_t)(best+1)]);
        s.gainDb=std::clamp(-0.55f*local,-4.5f,4.5f);
        out.items[(size_t)out.count++]=s;
    }
    return out;
}
} // namespace mixpilot

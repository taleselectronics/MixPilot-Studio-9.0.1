#include "../Source/SpectralProfiler.h"
#include "../Source/SaturationMath.h"
#include "../Source/ReferenceEQ.h"
#include <cmath>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <algorithm>

static void check(bool ok,const char* why){
    if(!ok){std::cerr<<"FAILED: "<<why<<'\n';std::exit(EXIT_FAILURE);}
}
static int region(float hz) {
    return std::clamp((int)(std::log(hz/30.0f)/std::log(16000.0f/30.0f)*18),0,17);
}
static std::vector<float> tone(double sampleRate,double seconds,float a,float b){
    const auto total=(size_t)(sampleRate*seconds);
    std::vector<float> result(total);
    const double pi=3.1415926535897932384626433832795;
    for(size_t i=0;i<total;++i){
        const double t=i/sampleRate;
        const float gate=(i%(size_t)sampleRate<(size_t)(sampleRate*0.8))?1.0f:0.0f;
        result[i]=gate*(a*std::sin(2*pi*240*t)+b*std::sin(2*pi*3200*t));
    }
    return result;
}
int main(){
    using mixpilot::SpectralProfiler;
    constexpr double rate=48000;
    const auto waveform=tone(rate,6,0.09f,0.018f);
    SpectralProfiler wav(rate),live(rate),scaled(rate),reference(rate),silent(rate);
    // WAV path streams block-wise, live input is fed incrementally. Same
    // measured signal MUST yield the same profile, not just similar visuals.
    for(size_t pos=0;pos<waveform.size();pos+=8192){
        auto count=std::min<size_t>(8192,waveform.size()-pos);
        wav.push(waveform.begin()+pos,waveform.begin()+pos+count);
    }
    for(float x:waveform){live.push(x);scaled.push(x*1.5f);silent.push(0.0f);}
    const auto other=tone(rate,6,0.018f,0.09f);
    reference.push(other.begin(),other.end());
    const auto a=wav.result(), b=live.result(), c=scaled.result(), r=reference.result();
    check(a.valid && b.valid && c.valid && r.valid,"18-band profiles created");
    check(!silent.result().valid,"silence is not a valid spectral reference");
    check(a.activeFrames>=5,"profile based on several valid windows");
    for(int k=0;k<18;++k){
        check(std::abs(a.relativeDb[(size_t)k]-b.relativeDb[(size_t)k])<0.0002f,
              "WAV and live analysis calculations agree");
        check(std::abs(a.relativeDb[(size_t)k]-c.relativeDb[(size_t)k])<0.03f,
              "normalized profile remains stable under gain changes");
    }
    check(a.relativeDb[(size_t)region(240)]>r.relativeDb[(size_t)region(240)]+4,
          "low-mid reference difference detected");
    check(r.relativeDb[(size_t)region(3200)]>a.relativeDb[(size_t)region(3200)]+4,
          "presence reference difference detected");
    const auto suggested=mixpilot::referenceEqSuggestions(a.relativeDb,r.relativeDb);
    check(suggested.count>=2,"reference differences produce multiple actionable EQ bands");
    bool foundLow=false,foundHigh=false;
    for(int i=0;i<suggested.count;++i){
        const auto& item=suggested.items[(size_t)i];
        if(item.hz>150&&item.hz<400&&item.gainDb<0)foundLow=true;
        if(item.hz>2000&&item.hz<4500&&item.gainDb>0)foundHigh=true;
        check(item.gainDb<=4.5f && item.gainDb>=-4.5f,
            "measured EQ gain must be conservative");
    }
    check(foundLow&&foundHigh,"measured EQ suggests the right tonal directions");
    // The profiler also supports common DAW sample rates beyond 48 kHz.
    for(double otherRate : {44100.0,96000.0}){
        const auto otherAudio=tone(otherRate,6,0.06f,0.025f);
        SpectralProfiler instrument(otherRate);
        instrument.push(otherAudio.begin(),otherAudio.end());
        check(instrument.result().valid,"spectral profile at common host sample rate");
    }
    // Harmonic growth must be real, not just an output gain increase.
    auto harmonic=[&](float amount,int harmonicNumber){
        constexpr int N=8192,period=128;
        const float drive=mixpilot::saturationDrive(amount);
        const float makeup=mixpilot::saturationMakeup(drive);
        double re=0,im=0;
        constexpr double pi=3.1415926535897932384626433832795;
        for(int n=0;n<N;++n){
            const double phase=2*pi*n/period;
            const float x=0.08f*std::sin(phase);
            const float y=mixpilot::tubeCurve(x,0,drive,makeup);
            re+=y*std::cos(phase*harmonicNumber);
            im+=y*std::sin(phase*harmonicNumber);
        }
        return std::sqrt(re*re+im*im);
    };
    const double low=harmonic(0.05f,3)/harmonic(0.05f,1);
    const double high=harmonic(1.0f,3)/harmonic(1.0f,1);
    check(high>low*8 && high>0.03,"drive creates audible 3rd harmonic growth");
    check(mixpilot::saturationDrive(1.0f)>mixpilot::saturationDrive(0.5f),
          "drive increasing");
    check(mixpilot::saturationBypass(0.21f,0.7f,0)==0.21f,
          "zero drive is transparent before output trim");
    check(std::abs(mixpilot::tubeCurve(1.0f,0,20.0f,mixpilot::saturationMakeup(20.0f)))<1.0f,
          "saturation bounded for full-scale input");
    std::cout << "Shared live/WAV spectrum and saturation transfer tests passed\n";
}

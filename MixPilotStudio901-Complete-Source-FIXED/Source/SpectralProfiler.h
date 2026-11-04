#pragma once
// A shared, offline (never audio-callback) streaming spectral profile for both
// loaded source/reference WAVs and captured Mixcraft audio.
// No JUCE dependency, so signal-processing invariants can be tested natively.
#include <array>
#include <cmath>
#include <complex>
#include <algorithm>
#include <cstddef>
#include <memory>

namespace mixpilot {
struct Profile18 {
    static constexpr int bands=18;
    std::array<float,bands> relativeDb{};
    int activeFrames=0;
    bool valid=false;
};
class SpectralProfiler final {
public:
    static constexpr int order=13, fftSize=1<<order;
    explicit SpectralProfiler(double sr) : sampleRate(sr>8000.0?sr:8000.0) {
        constexpr double pi=3.1415926535897932384626433832795;
        for(int i=0;i<fftSize;++i)
            scratch->window[(size_t)i]=(float)(0.5-0.5*std::cos(2.0*pi*i/(fftSize-1)));
        for(int i=0;i<=fftSize/2;++i){
            const double a=-2.0*pi*i/fftSize;
            scratch->twiddles[(size_t)i]={(float)std::cos(a),(float)std::sin(a)};
        }
        scratch->binIndex.fill(-1);
        for(int j=3;j<fftSize/2;++j){
            const double hz=j*sampleRate/fftSize;
            if(hz<30.0||hz>16000.0)continue;
            scratch->binIndex[(size_t)j]=std::clamp((int)(std::log(hz/30.0)/
                std::log(16000.0/30.0)*Profile18::bands),0,Profile18::bands-1);
        }
    }
    // The caller streams mono at the recording's original sample rate.
    void push(float mono) {
        if(!std::isfinite(mono))mono=0;
        scratch->samples[(size_t)fill++]=mono;
        if(fill==fftSize){accumulate();fill=0;}
    }
    template<class Iterator> void push(Iterator begin,Iterator end){
        for(;begin!=end;++begin)push(*begin);
    }
    Profile18 result() const {
        Profile18 r;r.activeFrames=activeFrames;
        if(activeFrames<5)return r;
        std::array<double,Profile18::bands> powers{};
        double sum=0;
        for(int b=0;b<Profile18::bands;++b){
            // Too few bins at low sample rates: do not declare a false match.
            if(binCount[(size_t)b]==0)return r;
            powers[(size_t)b]=power[(size_t)b]/binCount[(size_t)b];
            sum+=powers[(size_t)b];
        }
        const double mean=sum/Profile18::bands;
        if(mean<=1e-12)return r;
        for(int b=0;b<Profile18::bands;++b){
            const double db=10.0*std::log10(std::max(1e-15,powers[(size_t)b])/mean);
            r.relativeDb[(size_t)b]=(float)std::clamp(db,-48.0,24.0);
        }
        r.valid=true;
        return r;
    }
private:
    void accumulate(){
        double energy=0.0;
        for(float x:scratch->samples)energy+=(double)x*x;
        if(energy/fftSize<1e-7)return; // Silence and noise-only windows ignored.
        for(int i=0;i<fftSize;++i){
            unsigned int x=(unsigned int)i,rev=0;
            for(int bit=0;bit<order;++bit){rev=(rev<<1)|(x&1u);x>>=1u;}
            scratch->transformed[(size_t)rev]={scratch->samples[(size_t)i]*scratch->window[(size_t)i],0.0f};
        }
        for(int len=2;len<=fftSize;len*=2){
            const int half=len/2,stride=fftSize/len;
            for(int start=0;start<fftSize;start+=len){
                for(int j=0;j<half;++j){
                    const auto u=scratch->transformed[(size_t)(start+j)];
                    const auto v=scratch->transformed[(size_t)(start+j+half)]*scratch->twiddles[(size_t)(j*stride)];
                    scratch->transformed[(size_t)(start+j)]=u+v;
                    scratch->transformed[(size_t)(start+j+half)]=u-v;
                }
            }
        }
        for(int j=3;j<fftSize/2;++j){
            const int b=scratch->binIndex[(size_t)j];
            if(b<0)continue;
            power[(size_t)b]+=std::norm(scratch->transformed[(size_t)j]);
            ++binCount[(size_t)b];
        }
        ++activeFrames;
    }
    // FFT work buffers are intentionally heap-allocated. Five analyzers in
    // the regression test used nearly 1 MB of stack on Windows/MSVC and
    // caused a stack-overflow crash (CTest reported it as a segfault).
    // This allocation happens once when the offline analyzer is constructed;
    // push() and accumulate() never allocate memory.
    struct Scratch {
        std::array<float,fftSize> samples{}, window{};
        std::array<std::complex<float>,fftSize> transformed{};
        std::array<std::complex<float>,fftSize/2+1> twiddles{};
        std::array<int,fftSize/2> binIndex{};
    };
    double sampleRate;
    std::unique_ptr<Scratch> scratch = std::make_unique<Scratch>();
    std::array<double,Profile18::bands> power{};
    std::array<int,Profile18::bands> binCount{};
    int fill=0,activeFrames=0;
};
} // namespace mixpilot

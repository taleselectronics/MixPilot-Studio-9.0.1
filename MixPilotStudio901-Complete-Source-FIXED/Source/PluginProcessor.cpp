#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SaturationMath.h"
#include <algorithm>
#include <cmath>

namespace {
float db(float x){return juce::Decibels::gainToDecibels(juce::jmax(x,1.e-9f));}
float amp(float x){return juce::Decibels::decibelsToGain(x);}
const float diagnosticHz[]{130,250,460,850,1550,2850,4600,7100,10700};
const char* slotKeys[]{"type","enable","freq","gain","q","amount","ratio","attack","release","mix","slope"};
juce::String id(int i,const char* key){return "s"+juce::String(i+1)+key;}
juce::String dynamicKey(int slot,int band,const char* suffix){
    return id(slot,"b")+juce::String(band+1)+"dyn"+suffix;
}
}
juce::StringArray MixPilotProcessor::typeNames(){return {"Empty","Parametric EQ","Low Cut / HPF","High Cut / LPF","Compressor","De-esser","Saturation","Gain / Trim","Stereo Width","Echo Delay","Precision Limiter","Plate Reverb","Analog Phaser (DaisySP)","Opto Leveler","Studio Chorus","Studio Flanger"};}
juce::StringArray MixPilotProcessor::eqFilterNames(){return {"Bell","Low Shelf","High Shelf","Notch","High Pass","Low Pass"};}
juce::IIRCoefficients MixPilotProcessor::bandCoefficients(double sr,int filterType,float frequency,float q,float gain){
    const auto hz=juce::jlimit(20.0, sr*0.44, (double)frequency);
    const auto width=juce::jlimit(0.3,12.0,(double)q);
    const auto level=juce::Decibels::decibelsToGain(gain);
    switch(filterType){
        case 1:return juce::IIRCoefficients::makeLowShelf(sr,hz,width,level);
        case 2:return juce::IIRCoefficients::makeHighShelf(sr,hz,width,level);
        case 3:return juce::IIRCoefficients::makeNotchFilter(sr,hz,width);
        case 4:return juce::IIRCoefficients::makeHighPass(sr,hz,width);
        case 5:return juce::IIRCoefficients::makeLowPass(sr,hz,width);
        default:return juce::IIRCoefficients::makePeakFilter(sr,hz,width,level);
    }
}
MixPilotProcessor::MixPilotProcessor()
 : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
                                   .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
   apvts(*this,nullptr,"PARAMETERS",makeLayout())
{
    for(auto& p:ring)p.store(0);
    for(auto& p:compressorMeters)p.store(0);
    for(auto& p:compressorDetectors)p.store(-90);
    for(auto& p:deEsserMeters)p.store(0);
    for(auto& p:limiterMeters)p.store(0);
    for(auto& p:deEsserDetectors)p.store(-90);
    for(auto& p:measured)p.store(-90);
    for(auto& p:fileMeasured)p.store(-90);
}
juce::AudioProcessorValueTreeState::ParameterLayout MixPilotProcessor::makeLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto add=[&](const String& key,const String& label,float lo,float hi,float initial,float step=0.01f){
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{key,1},label,NormalisableRange<float>(lo,hi,step),initial));
    };
    auto choice=[&](const String& key,const String& label,const StringArray& options,int initial){
        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{key,1},label,options,initial));
    };
    choice("mode","Source",{"Universal","Vocals","Instrument","Drums","Stereo Mix"},1);
    choice("mic","Microphone",{"Unknown / Flat","Neumann TLM 103","Generic dynamic microphone"},1);
    add("output","Master output dB",-24,12,0);
    add("wet","Global wet percentage",0,100,100);
    add("intensity","AI strength percentage",0,100,80);
    for(int i=0;i<slots;++i){
        const int defaults[]={2,1,4,1,5,6,3,0,0,0};
        choice(id(i,"type"),"Module "+String(i+1)+" type",typeNames(),defaults[i]);
        p.push_back(std::make_unique<AudioParameterBool>(ParameterID{id(i,"enable"),1},"Module enabled",true));
        add(id(i,"freq"),"Module frequency Hz",20,20000,i==0?75:i==4?6500:i==6?16000:1200,1);
        add(id(i,"gain"),"Module EQ/gain dB",-18,18,0);
        add(id(i,"q"),"EQ Q",0.3f,12,1.1f);
        add(id(i,"amount"),"Processing amount",0,100,i==2?42:30);
        // The dedicated de-esser settings are persistent and available to Qwen.
        // New parameters do not modify pre-existing EQ/compressor parameter IDs.
        add(id(i,"deessThreshold"),"De-esser threshold dBFS",-60,0,-30,0.1f);
        add(id(i,"deessRange"),"De-esser maximum reduction dB",0,18,8,0.1f);
        // Dedicated saturation parameters are added without renaming legacy IDs.
        // Final-output limiter parameters. Persist independently of old amount/mix IDs.
        add(id(i,"limInput"),"Limiter input gain dB",-12,18,0,0.1f);
        add(id(i,"limCeiling"),"Limiter output ceiling dBTP",-12,-0.1f,-1.0f,0.1f);
        add(id(i,"limRelease"),"Limiter release ms",40,1000,140,1);
        p.push_back(std::make_unique<AudioParameterBool>(ParameterID{id(i,"limTruePeak"),1},"Oversampled peak detection",true));
        choice(id(i,"satMode"),"Saturation character",{"Tube","Tape","Transformer"},0);
        choice(id(i,"satQuality"),"Saturation oversampling",{"1x","2x","4x"},0);
        add(id(i,"satTone"),"Saturation tone",-100,100,0,1);
        add(id(i,"satBias"),"Saturation bias or character",-100,100,0,1);
        add(id(i,"satOutput"),"Saturation output trim dB",-18,12,0,0.1f);
        choice(id(i,"deessMode"),"De-esser processing mode",{"Split band","Wideband"},0);
        // New spatial controls have dedicated IDs: legacy slot controls stay intact.
        choice(id(i,"spaceMode"),"Reverb algorithm",{"Plate","Room","Hall"},0);
        add(id(i,"spaceDecay"),"Reverb decay seconds",0.3f,8.0f,2.0f,0.01f);
        add(id(i,"spacePredelay"),"Reverb pre-delay ms",0.0f,150.0f,25.0f,1.0f);
        add(id(i,"spaceDamp"),"Reverb damping percent",0,100,50,1);
        add(id(i,"spaceLowCut"),"Reverb low cut Hz",20,1000,160,1);
        add(id(i,"spaceHighCut"),"Reverb high cut Hz",2000,18000,10000,1);
        choice(id(i,"delayMode"),"Delay mode",{"Stereo","Ping Pong","Tape Echo"},0);
        add(id(i,"delayMs"),"Delay time ms",30,1000,275,1);
        p.push_back(std::make_unique<AudioParameterBool>(ParameterID{id(i,"delaySync"),1},"Delay sync to tempo",false));
        choice(id(i,"delayDivision"),"Tempo division",{"1/4","1/8","Dotted 1/8","1/16"},1);
        add(id(i,"delayFeedback"),"Delay feedback percent",0,85,28,1);
        add(id(i,"delayTone"),"Delay darkness percent",0,100,35,1);
        add(id(i,"delayDuck"),"Delay ducking percent",0,100,40,1);
        add(id(i,"modRate"),"Modulation rate Hz",0.05f,8,0.65f,0.01f);
        add(id(i,"modDepth"),"Modulation depth percent",0,100,55,1);
        add(id(i,"modFeedback"),"Modulation feedback percent",-85,85,0,1);
        add(id(i,"modStereo"),"Modulation stereo phase degrees",0,180,90,1);
        add(id(i,"ratio"),"Compressor ratio",1,12,3);
        add(id(i,"attack"),"Attack ms",0.1f,200,12);
        add(id(i,"release"),"Release ms",10,1000,115);
        add(id(i,"mix"),"Module wet",0,100,100);
        choice(id(i,"slope"),"Cut slope",{"12 dB/oct","24 dB/oct"},0);
        // Dynamic EQ is explicitly opt-in; legacy sessions remain sonically unchanged.
        for(int band=0;band<eqBands;++band){
            const auto prefix=dynamicKey(i,band,"");
            p.push_back(std::make_unique<AudioParameterBool>(ParameterID{prefix+"on",1},"Band dynamic processing enabled",false));
            add(prefix+"threshold","Dynamic EQ threshold dBFS",-60,0,-24,0.1f);
            add(prefix+"range","Dynamic EQ max gain change dB",-12,12,-4,0.1f);
            add(prefix+"attack","Dynamic EQ attack ms",1,100,12,0.1f);
            add(prefix+"release","Dynamic EQ release ms",20,600,120,1);
        }
        // Each EQ module owns a whole 12-band equalizer. Band 1 retains legacy
        // freq/gain/q parameter IDs, so pre-existing AI-generated settings work.
        const float centres[]{70,120,200,330,550,900,1500,2500,4100,6800,10500,16000};
        choice(id(i,"eqtype"),"EQ band 1 filter type",eqFilterNames(),0);
        p.push_back(std::make_unique<AudioParameterBool>(ParameterID{id(i,"eqon"),1},"EQ band 1 active",true));
        for(int band=1;band<eqBands;++band){
            const auto stem=id(i,"b")+String(band+1);
            add(stem+"f","EQ band frequency",20,20000,centres[band],1);
            add(stem+"g","EQ band gain",-18,18,0);
            add(stem+"q","EQ band width Q",0.3f,12,1.1f);
            p.push_back(std::make_unique<AudioParameterBool>(ParameterID{stem+"on",1},"EQ band active",true));
            choice(stem+"type","EQ band filter type",eqFilterNames(),0);
        }
    }
    return {p.begin(),p.end()};
}
bool MixPilotProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto input=l.getMainInputChannelSet();
    return (input==juce::AudioChannelSet::mono() || input==juce::AudioChannelSet::stereo()) && input==l.getMainOutputChannelSet();
}
double MixPilotProcessor::getTailLengthSeconds() const {
    // Inform Mixcraft about measurable effect tails, not limiter latency.
    // This is an estimate: feedback, automation and wet mixing affect the end.
    double longest = 0.0;
    for (int i = 0; i < slots; ++i) {
        if (value(id(i,"enable")) < 0.5f) continue;
        const int type = (int)value(id(i,"type"));
        if (type == 11) {
            longest = std::max(longest, mixpilot::reverbTailSeconds(
                value(id(i,"spaceDecay")), value(id(i,"spacePredelay")) * 0.001));
        } else if (type == 9) {
            // Tempo-synced echoes can be longer in slow projects. Conservatively
            // report a one-second delay if sync is enabled (no host query here).
            const double delaySeconds = value(id(i,"delaySync")) > 0.5f
                ? 1.0 : value(id(i,"delayMs")) * 0.001;
            const double feedback = value(id(i,"delayFeedback")) * 0.01;
            longest = std::max(longest, mixpilot::delayTailSeconds(delaySeconds,feedback));
        } else if (type == 14 || type == 15 || type == 12) {
            longest = std::max(longest,0.15);
        }
    }
    return std::min(longest,60.0);
}
float MixPilotProcessor::value(const juce::String& key) const {auto* p=apvts.getRawParameterValue(key);return p?p->load():0;}
void MixPilotProcessor::setValue(const juce::String& key,float v){
    if(auto* p=apvts.getParameter(key)){
        auto range=p->getNormalisableRange();
        p->setValueNotifyingHost(range.convertTo0to1(juce::jlimit(range.start,range.end,v)));
    }
}
void MixPilotProcessor::setSlotType(int s,int type){
    if(s<0||s>=slots)return;
    const int previous=(int)value(id(s,"type"));
    const int next=juce::jlimit(0,types-1,type);
    setValue(id(s,"type"),(float)next);
    // New spatial modules open as gentle insert effects, not 100% wet.
    // Existing saved settings remain under their original APVTS IDs.
    if(next!=previous && (next==9||next==11||next==14||next==15))
        setValue(id(s,"mix"),25.0f);
}
void MixPilotProcessor::swapSlots(int a,int b){
    if(a<0||b<0||a>=slots||b>=slots||a==b)return;
    juce::StringArray keys;
    for(const auto* k:slotKeys)keys.add(k);
    keys.add("eqtype");keys.add("eqon");
    keys.add("deessThreshold");keys.add("deessRange");keys.add("deessMode");
    for(const auto* key: {"satMode","satQuality","satTone","satBias","satOutput",
                           "limInput","limCeiling","limRelease","limTruePeak",
                           "spaceMode","spaceDecay","spacePredelay","spaceDamp","spaceLowCut","spaceHighCut",
                           "delayMode","delayMs","delaySync","delayDivision","delayFeedback","delayTone","delayDuck",
                           "modRate","modDepth","modFeedback","modStereo"})keys.add(key);
    for(int band=0;band<eqBands;++band)
        for(const auto* suffix:{"on","threshold","range","attack","release"})
            keys.add("b"+juce::String(band+1)+"dyn"+suffix);
    for(int n=2;n<=eqBands;++n){
        const auto stem="b"+juce::String(n);
        for(const auto* suffix:{"f","g","q","on","type"})keys.add(stem+suffix);
    }
    for(const auto& k:keys){
        const auto oldA=value("s"+juce::String(a+1)+k);
        const auto oldB=value("s"+juce::String(b+1)+k);
        setValue("s"+juce::String(a+1)+k,oldB);
        setValue("s"+juce::String(b+1)+k,oldA);
    }
}
void MixPilotProcessor::prepareToPlay(double rate,int){
    sampleRate=juce::jmax(8000.0,rate);
    targetFrames=(int64_t)(sampleRate*6.0);
    liveCaptureMono.resize((size_t)targetFrames); // prepareToPlay only, not the audio thread
    liveCaptureProcessed.resize((size_t)targetFrames);
    capturing=false;frames=0;captured.store(false);
    for(auto& m:compressorMeters)m.store(0);
    for(auto& m:compressorDetectors)m.store(-90);
    for(auto& m:deEsserMeters)m.store(0);
    for(auto& m:deEsserDetectors)m.store(-90);
    for(auto& ch:stage)for(auto& s:ch){
        s.filter.reset();s.second.reset();s.deEssLow.reset();
        s.deEssEnvelope=0.0f;s.deEssGainDb=0.0f;
        for(auto& eq:s.extraEQ)eq.reset();
        for(auto& d:s.dynamicEQ){d.detector.reset();d.colourFilter.reset();d.envelope=0;d.wet=0;}
        s.envelope=0;s.compGain=1;s.sibilanceLp=0;
        s.satPrevious=s.satTapeMemory=s.satTransformerLow=0.0f;
        s.satToneLow=s.satDcInput=s.satDcOutput=0.0f;
        s.echoDelay.resize((int)(sampleRate*1.10));s.echoDelay.reset();
        s.predelay.assign((size_t)(sampleRate*.16)+2,0.0f);s.predelayWrite=0;
        s.modulationDelay.assign((size_t)(sampleRate*.065)+2,0.0f);s.modulationWrite=0;
        s.modPhase=s.modFeedbackSample=0.0f;
        s.delayToneState=s.delayLast=s.delayDuckEnv=0.0f;
        s.reverbLow=s.reverbHigh=0.0f;
        s.phaser.Init((float)sampleRate);
        s.phaser.SetPoles(4);
        s.phaser.SetFreq(750.0f);
        s.phaser.SetLfoFreq(0.55f);
        s.phaser.SetLfoDepth(0.6f);
        s.phaser.SetFeedback(0.1f);
        for(int t=0;t<3;++t){
            // 3 variable-length FDN-style combs (fixed memory, variable tap length).
            s.reverbDelay[(size_t)t].assign((size_t)juce::jmax(32.0,sampleRate*.17),0.0f);
            s.reverbPos[(size_t)t]=0;s.reverbDamp[(size_t)t]=0;
        }
    }
    for(auto& ch:detector)for(auto& f:ch)f.reset();
    for(auto& s:lowState)s=0; for(auto& s:highState)s=0;
    safetyGain=1.0f;
    limiterEnvelope=1.0f;
    wetSmoother.reset(juce::jlimit(0.0f,1.0f,value("wet")*0.01f));
    outputGainSmoother.reset(amp(value("output")));
    limiterInputSmoother.reset(1.0f);
    // 5 ms fixed processing latency, including bypass/no-limiter states.
    // Allocate only here, never in processBlock.
    fixedLookahead=juce::jmax(1,(int)std::round(sampleRate*0.005));
    setLatencySamples(fixedLookahead);
    for(auto& d:outputDelay)d.assign((size_t)fixedLookahead+1,0.0f);
    delayWrite=0;peakHistoryWrite=0;
    for(auto& history:peakHistory)history.fill(0.0f);
    // Four fractional phases of a symmetric 25-tap windowed-sinc interpolator.
    // The detector reports the reconstructed peaks 12 samples late; the
    // 5 ms audio delay provides ample time for gain reduction before output.
    constexpr double pi=3.14159265358979323846;
    for(int phase=0;phase<limiterPhases;++phase){
        const double fraction=(double)phase/(double)limiterPhases;
        double sum=0.0;
        for(int tap=0;tap<limiterFirTaps;++tap){
            const double offset=12.0-(double)tap-fraction;
            const double sinc=std::abs(offset)<1.0e-9?1.0:std::sin(pi*offset)/(pi*offset);
            const double window=0.5-0.5*std::cos(2.0*pi*(double)tap/(limiterFirTaps-1));
            const float coefficient=(float)(sinc*window);
            peakFir[(size_t)phase][(size_t)tap]=coefficient;
            sum+=coefficient;
        }
        if(std::abs(sum)>1.0e-9)
            for(auto& v:peakFir[(size_t)phase])v/=(float)sum;
    }
    for(auto& meter:limiterMeters)meter.store(0.0f,std::memory_order_relaxed);
    for(auto& slotMeters:dynamicMeter)
        for(auto& meter:slotMeters)meter.store(0.0f,std::memory_order_relaxed);
    updateFilters(); filterCountdown=0;
}
void MixPilotProcessor::updateFilters(){
    for(int i=0;i<slots;++i){
        const int type=(int)value(id(i,"type"));
        const double hz=juce::jlimit(20.0,sampleRate*.44,(double)value(id(i,"freq")));
        const double q=juce::jlimit(.3,12.0,(double)value(id(i,"q")));
        juce::IIRCoefficients co;
        if(type==2)co=juce::IIRCoefficients::makeHighPass(sampleRate,hz);
        else if(type==3)co=juce::IIRCoefficients::makeLowPass(sampleRate,hz);
        else co=value(id(i,"eqon"))>.5f
                ? bandCoefficients(sampleRate,(int)value(id(i,"eqtype")),(float)hz,(float)q,value(id(i,"gain")))
                : juce::IIRCoefficients(1.0,0.0,0.0,1.0,0.0,0.0);
        for(auto& channel:stage){
            auto& st=channel[(size_t)i];st.filter.setCoefficients(co);st.second.setCoefficients(co);
            // Second-order low-pass used to reconstruct the complementary high band.
            // The split sum is low + (input-low), avoiding a phase-mismatched second path.
            st.deEssLow.setCoefficients(juce::IIRCoefficients::makeLowPass(
                sampleRate,juce::jlimit(2000.0,sampleRate*.44,(double)value(id(i,"freq"))),0.70710678));
            for(int band=0;band<eqBands;++band){
                const auto prefix=band==0?id(i,""):id(i,"b")+juce::String(band+1);
                const float freq=band==0?value(id(i,"freq")):value(prefix+"f");
                const float qBand=band==0?value(id(i,"q")):value(prefix+"q");
                const int shape=(int)(band==0?value(id(i,"eqtype")):value(prefix+"type"));
                const float range=value(dynamicKey(i,band,"range"));
                auto& dyn=st.dynamicEQ[(size_t)band];
                const double centre=juce::jlimit(20.0,sampleRate*.44,(double)freq);
                dyn.detector.setCoefficients(juce::IIRCoefficients::makeBandPass(sampleRate,centre,juce::jlimit(.3,12.0,(double)qBand)));
                // Full-range response; per-sample crossfade supplies click-free dynamic depth.
                dyn.colourFilter.setCoefficients(bandCoefficients(sampleRate,shape,freq,qBand,range));
            }
            for(int band=1;band<eqBands;++band){
                const auto prefix=id(i,"b")+juce::String(band+1);
                const auto f=juce::jlimit(20.0,sampleRate*.44,(double)value(prefix+"f"));
                const auto qBand=juce::jlimit(0.3,12.0,(double)value(prefix+"q"));
                const auto active=value(prefix+"on")>.5f;
                // Disabled bands are an identity biquad, including for cut/notch filters.
                st.extraEQ[(size_t)(band-1)].setCoefficients(active
                    ? bandCoefficients(sampleRate,(int)value(prefix+"type"),(float)f,(float)qBand,value(prefix+"g"))
                    : juce::IIRCoefficients(1.0,0.0,0.0,1.0,0.0,0.0));
            }
        }
    }
    for(int i=0;i<diagnosticBands;++i){
        auto co=juce::IIRCoefficients::makeBandPass(sampleRate,juce::jmin((double)diagnosticHz[i],sampleRate*.44),2.3);
        for(auto& ch:detector)ch[(size_t)i].setCoefficients(co);
    }
}
void MixPilotProcessor::startAnalysis() noexcept {fileValid.store(false);captured.store(false);analysisRequested.store(true);}
bool MixPilotProcessor::copyLiveCapture(std::vector<float>& output) const {
    // Completion is published only after the callback stops writing this buffer.
    if(!captured.load(std::memory_order_acquire) ||
       liveCaptureMono.size()<(size_t)targetFrames || targetFrames<=0)return false;
    output.assign(liveCaptureMono.begin(),liveCaptureMono.begin()+(size_t)targetFrames);
    return true;
}
bool MixPilotProcessor::copyProcessedCapture(std::vector<float>& output) const {
    if(!captured.load(std::memory_order_acquire) ||
       liveCaptureProcessed.size()<(size_t)targetFrames || targetFrames<=0)return false;
    output.assign(liveCaptureProcessed.begin(),liveCaptureProcessed.begin()+(size_t)targetFrames);
    return true;
}
void MixPilotProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){
    juce::ScopedNoDenormals nd;
    const int count=b.getNumSamples(), chans=juce::jmin(2,b.getNumChannels());
    if(count<=0||chans<1)return;
    if(analysisRequested.exchange(false)){
        capturing=true;captured.store(false);frames=0;rmsSum=lowSum=midSum=highSum=0;peak=0;
        leftEnergy=rightEnergy=crossEnergy=0;
        quietWindowEnergy=0;minimumWindowEnergy=1.0e30;quietWindowFrames=0;
        captureStereo=(chans==2);
        diagnosticSum.fill(0);
        for(auto& ch:detector)for(auto& f:ch)f.reset();
    }
    if(filterCountdown<=0){updateFilters();filterCountdown=(int)(sampleRate*.07);}
    filterCountdown-=count;
    // Snapshot APVTS parameters ONCE per audio block. Never do string/map lookups
    // inside the inner sample loops: the previous implementation could perform
    // millions of parameter lookups per second and starve the host audio thread.
    float hostBpm=120.0f;
    if(auto* playHead=getPlayHead()){
        if(auto position=playHead->getPosition()){
            if(auto bpm=position->getBpm()){
                if(std::isfinite(*bpm) && *bpm>20.0)hostBpm=(float)*bpm;
            }
        }
    }
    std::array<Settings,slots> settings{};
    for (int i=0;i<slots;++i) {
        auto& cfg=settings[(size_t)i];
        cfg.type=(int)value(id(i,"type"));
        cfg.enabled=value(id(i,"enable"))>=.5f;
        if (!cfg.enabled || cfg.type==0) continue;
        cfg.frequency=value(id(i,"freq"));
        cfg.gain=value(id(i,"gain"));
        cfg.q=value(id(i,"q"));
        cfg.amount=value(id(i,"amount"))*.01f;
        cfg.ratio=juce::jmax(1.0f,value(id(i,"ratio")));
        cfg.attackMs=juce::jmax(.1f,value(id(i,"attack")));
        cfg.releaseMs=juce::jmax(10.0f,value(id(i,"release")));
        cfg.mix=juce::jlimit(0.0f,1.0f,value(id(i,"mix"))*.01f);
        cfg.slope=(int)value(id(i,"slope"));
        if(cfg.type==6){
            cfg.satMode=juce::jlimit(0,2,(int)value(id(i,"satMode")));
            cfg.satOversample=1 << juce::jlimit(0,2,(int)value(id(i,"satQuality")));
            cfg.satTone=juce::jlimit(-1.0f,1.0f,value(id(i,"satTone"))*.01f);
            cfg.satBias=juce::jlimit(-1.0f,1.0f,value(id(i,"satBias"))*.01f);
            cfg.satTrim=amp(juce::jlimit(-18.0f,12.0f,value(id(i,"satOutput"))));
            const float oversampledRate=(float)sampleRate*(float)cfg.satOversample;
            cfg.satToneCoeff=std::exp(-2.0f*juce::MathConstants<float>::pi*
                (2800.0f+juce::jmax(0.0f,-cfg.satTone)*4500.0f)/oversampledRate);
            cfg.satTapeCoeff=std::exp(-1.0f/(oversampledRate*.003f));
            cfg.satLfCoeff=std::exp(-2.0f*juce::MathConstants<float>::pi*150.0f/oversampledRate);
            cfg.satDcCoeff=std::exp(-2.0f*juce::MathConstants<float>::pi*8.0f/(float)sampleRate);
        }
        if(cfg.type==10){
            cfg.limInput=amp(value(id(i,"limInput")));
            cfg.limCeiling=amp(value(id(i,"limCeiling")));
            const float releaseMs=juce::jlimit(40.0f,1000.0f,value(id(i,"limRelease")));
            cfg.limReleaseCoef=std::exp(-1.0f/(float)(sampleRate*releaseMs*.001f));
            cfg.limTruePeak=value(id(i,"limTruePeak"))>.5f;
        }
        if(cfg.type==5){
            cfg.deessThreshold=value(id(i,"deessThreshold"));
            cfg.deessRange=juce::jlimit(0.0f,18.0f,value(id(i,"deessRange")));
            cfg.deessWide=value(id(i,"deessMode"))>.5f;
        }
        cfg.attackCoef=std::exp(-1.0f/(float)(sampleRate*cfg.attackMs*.001));
        cfg.releaseCoef=std::exp(-1.0f/(float)(sampleRate*cfg.releaseMs*.001));
        cfg.sibilanceCoef=std::exp(-2.0f*juce::MathConstants<float>::pi*juce::jlimit(2000.0f,12000.0f,cfg.frequency)/(float)sampleRate);
        if(cfg.type==9){
            cfg.delayMode=juce::jlimit(0,2,(int)value(id(i,"delayMode")));
            float milliseconds=value(id(i,"delayMs"));
            if(value(id(i,"delaySync"))>.5f){
                // JUCE PositionInfo tempo is an optional value; handle hosts without BPM.
                const int division=juce::jlimit(0,3,(int)value(id(i,"delayDivision")));
                const float beats[]={1.0f,0.5f,0.75f,0.25f};
                milliseconds=60000.0f/juce::jmax(20.0f,hostBpm)*beats[division];
            }
            cfg.delaySamples=(size_t)juce::jlimit(10.0,sampleRate*1.0,sampleRate*(double)milliseconds*.001);
            cfg.delayFeedback=juce::jlimit(0.0f,0.85f,value(id(i,"delayFeedback"))*.01f);
            cfg.delayToneCoef=std::exp(-2.0f*juce::MathConstants<float>::pi*
                juce::jmap(value(id(i,"delayTone"))*.01f,12000.0f,1200.0f)/(float)sampleRate);
            cfg.delayDuck=juce::jlimit(0.0f,1.0f,value(id(i,"delayDuck"))*.01f);
        }
        if(cfg.type==11){
            cfg.spaceMode=juce::jlimit(0,2,(int)value(id(i,"spaceMode")));
            cfg.spaceDecay=value(id(i,"spaceDecay"));
            cfg.spaceDamp=value(id(i,"spaceDamp"))*.01f;
            cfg.spacePreSamples=(size_t)(sampleRate*value(id(i,"spacePredelay"))*.001);
            cfg.spaceLowCoef=std::exp(-2.0f*juce::MathConstants<float>::pi*
                value(id(i,"spaceLowCut"))/(float)sampleRate);
            cfg.spaceHighCoef=std::exp(-2.0f*juce::MathConstants<float>::pi*
                value(id(i,"spaceHighCut"))/(float)sampleRate);
            const float bases[3][3]={{.0297f,.0371f,.0411f},
                                      {.0131f,.0177f,.0229f},
                                      {.0713f,.0899f,.1097f}};
            for(int tap=0;tap<3;++tap){
                const auto length=stage[0][(size_t)i].reverbDelay[(size_t)tap].size();
                cfg.spaceTapSamples[(size_t)tap]=juce::jlimit((size_t)2,length-1,
                    (size_t)(sampleRate*bases[cfg.spaceMode][tap]));
                cfg.spaceFeedback[(size_t)tap]=std::pow(0.001f,
                    (float)cfg.spaceTapSamples[(size_t)tap]/
                    juce::jmax(1.0f,(float)sampleRate*cfg.spaceDecay));
            }
        }
        if(cfg.type==14||cfg.type==15){
            cfg.modRate=value(id(i,"modRate"));
            cfg.modDepth=value(id(i,"modDepth"))*.01f;
            cfg.modFeedback=juce::jlimit(-.85f,.85f,value(id(i,"modFeedback"))*.01f);
            cfg.modStereo=value(id(i,"modStereo"));
        }
        if(cfg.type==12){
            // Set controls once per audio block, never once per sample.
            for(int channel=0;channel<2;++channel){
                auto& ph=stage[(size_t)channel][(size_t)i].phaser;
                ph.SetPoles(juce::jlimit(1,8,(int)std::round(cfg.ratio)));
                ph.SetFreq(juce::jlimit(80.0f,5000.0f,cfg.frequency));
                ph.SetLfoFreq(juce::jlimit(0.05f,4.0f,cfg.attackMs/50.0f));
                ph.SetLfoDepth(cfg.amount);
                ph.SetFeedback(juce::jlimit(0.0f,0.75f,(cfg.gain+18.0f)/48.0f));
            }
        }
    }
    std::array<std::array<DynamicSettings,eqBands>,slots> dynamicSettings{};
    for(int i=0;i<slots;++i){
        if(settings[(size_t)i].type!=1||!settings[(size_t)i].enabled)continue;
        for(int band=0;band<eqBands;++band){
            auto& d=dynamicSettings[(size_t)i][(size_t)band];
            const bool bandOn=band==0?value(id(i,"eqon"))>.5f:
                value(id(i,"b")+juce::String(band+1)+"on")>.5f;
            const int shape=(int)(band==0?value(id(i,"eqtype")):
                value(id(i,"b")+juce::String(band+1)+"type"));
            d.enabled=bandOn && shape<=2 && value(dynamicKey(i,band,"on"))>.5f;
            if(!d.enabled)continue;
            d.threshold=value(dynamicKey(i,band,"threshold"));
            const float attack=juce::jmax(1.0f,value(dynamicKey(i,band,"attack")));
            const float release=juce::jmax(20.0f,value(dynamicKey(i,band,"release")));
            d.attackCoef=std::exp(-1.0f/(float)(sampleRate*attack*.001));
            d.releaseCoef=std::exp(-1.0f/(float)(sampleRate*release*.001));
        }
    }
    // One limiter may be active as final output protector. When several are
    // present, the last enabled limiter slot supplies the output parameters.
    int limiterSlot=-1;
    for(int i=0;i<slots;++i)
        if(settings[(size_t)i].enabled && settings[(size_t)i].type==10)
            limiterSlot=i;
    // One target update per host block. Each smoothed value advances once per
    // sample, identically for both channels; 15 ms ramps avoid zipper clicks.
    const int smoothSamples=juce::jmax(1,(int)std::round(sampleRate*.015));
    wetSmoother.setTarget(juce::jlimit(0.0f,1.0f,value("wet")*.01f),smoothSamples);
    outputGainSmoother.setTarget(amp(value("output")),smoothSamples);
    const float limiterDrive=limiterSlot>=0?settings[(size_t)limiterSlot].limInput:1.0f;
    limiterInputSmoother.setTarget(limiterDrive,smoothSamples);
    float in=0,out=0,gr=0,blockLimiterGr=0.0f;
    std::array<float,slots> blockCompressorGr{};
    std::array<float,slots> blockDeEsserGr{};
    std::array<float,slots> blockDeEsserDb{};
    blockDeEsserDb.fill(-90.0f);
    std::array<float,slots> blockDetectorDb{};
    blockDetectorDb.fill(-90.0f);
    const float lowCoef=std::exp(-2.0f*juce::MathConstants<float>::pi*210.0f/(float)sampleRate);
    const float hiCoef=std::exp(-2.0f*juce::MathConstants<float>::pi*4700.0f/(float)sampleRate);
    for(int n=0;n<count;++n){
        const float globalWet=wetSmoother.next();
        const float globalOutput=outputGainSmoother.next();
        const float inGain=limiterInputSmoother.next();
        float dry[2]{},y[2]{};
        float mono=0;
        for(int c=0;c<chans;++c){
            dry[c]=b.getReadPointer(c)[n];
            if(!std::isfinite(dry[c]))dry[c]=0.0f;
            y[c]=dry[c];
            in=juce::jmax(in,std::abs(dry[c]));mono+=dry[c]/chans;
            if(capturing){
                const float x=dry[c];
                lowState[c]=lowCoef*lowState[c]+(1-lowCoef)*x;
                highState[c]=hiCoef*highState[c]+(1-hiCoef)*x;
                const float lo=lowState[c],hi=x-highState[c],mid=x-lo-hi;
                rmsSum+=x*x;lowSum+=lo*lo;midSum+=mid*mid;highSum+=hi*hi;
                peak=juce::jmax(peak,std::abs(x));
                for(int k=0;k<diagnosticBands;++k){const float d=detector[(size_t)c][(size_t)k].processSingleSampleRaw(x);diagnosticSum[(size_t)k]+=d*d;}
            }
        }
        if(capturing){
            const float left=dry[0];
            const float right=chans==2?dry[1]:0.0f;
            if(chans==2){
                leftEnergy+=(double)left*left;
                rightEnergy+=(double)right*right;
                crossEnergy+=(double)left*right;
            }
            quietWindowEnergy+=((double)left*left+(chans==2?(double)right*right:0.0))/(double)chans;
            if(frames+n<targetFrames)
                liveCaptureMono[(size_t)(frames+n)]=mono;
            if(++quietWindowFrames==1024){
                minimumWindowEnergy=juce::jmin(minimumWindowEnergy,quietWindowEnergy/1024.0);
                quietWindowFrames=0;quietWindowEnergy=0;
            }
        }
        const auto w=ringWrite.fetch_add(1,std::memory_order_relaxed);
        ring[(size_t)(w%spectrumSize)].store(mono,std::memory_order_relaxed);
        for(int i=0;i<slots;++i){
            const auto& cfg=settings[(size_t)i];
            const int type=cfg.type;
            if(type==0||!cfg.enabled)continue;
            const float amount=cfg.amount;
            const float mix=cfg.mix;
            float stageInput[2]{y[0],y[1]};
            for(int c=0;c<chans;++c){
                auto& st=stage[(size_t)c][(size_t)i];
                float x=y[c];
                ModuleFrame frame{dynamicSettings[(size_t)i],
                    &stage[(size_t)(1-c)][(size_t)i],stageInput[c],gr,
                    blockCompressorGr[(size_t)i],blockDetectorDb[(size_t)i],
                    blockDeEsserGr[(size_t)i],blockDeEsserDb[(size_t)i],c,chans};
                x=processModule(x,type,cfg,st,frame);
                y[c]=stageInput[c]*(1-mix)+x*mix;
            }
            if(type==8)processStereoWidth(y,stageInput,cfg,chans);
        }
        // The final stage has one fixed 5 ms delay for ALL states, so host
        // compensation is correct even when the user bypasses the limiter.
        // Detection uses stereo-linked input peaks; no audio-thread allocation.
        float result[2]{};
        const bool limiting=limiterSlot>=0;
        const auto* limiter=limiting ? &settings[(size_t)limiterSlot] : nullptr;
        float detectedPeak=0.0f;
        const int ringLength=fixedLookahead+1;
        for(int c=0;c<chans;++c){
            float v=(dry[c]*(1.0f-globalWet)+y[c]*globalWet)*globalOutput*inGain;
            if(!std::isfinite(v))v=0.0f;
            result[c]=v;
            detectedPeak=juce::jmax(detectedPeak,std::abs(v));
            peakHistory[(size_t)c][(size_t)peakHistoryWrite]=v;
        }
        if(limiting && limiter->limTruePeak){
            // Windowed-sinc 4x reconstructed peaks. This is an estimator,
            // not a guarantee of BS.1770 true-peak conformance.
            for(int c=0;c<chans;++c){
                for(int phase=0;phase<limiterPhases;++phase){
                    float reconstructed=0.0f;
                    for(int tap=0;tap<limiterFirTaps;++tap){
                        const int idx=(peakHistoryWrite+limiterFirTaps-tap)%limiterFirTaps;
                        reconstructed+=peakHistory[(size_t)c][(size_t)idx]*peakFir[(size_t)phase][(size_t)tap];
                    }
                    detectedPeak=juce::jmax(detectedPeak,std::abs(reconstructed));
                }
            }
        }
        peakHistoryWrite=(peakHistoryWrite+1)%limiterFirTaps;
        float targetGain=1.0f;
        if(limiting && detectedPeak>limiter->limCeiling)
            targetGain=limiter->limCeiling/juce::jmax(detectedPeak,1.0e-9f);
        if(limiting){
            // 0.4ms attack inside the 5ms lookahead window.
            const float attackCoef=std::exp(-1.0f/(float)(sampleRate*.0004));
            const float smoothing=targetGain<limiterEnvelope?attackCoef:limiter->limReleaseCoef;
            limiterEnvelope=smoothing*limiterEnvelope+(1.0f-smoothing)*targetGain;
        }else{
            limiterEnvelope=1.0f;
        }
        const int readIndex=(delayWrite+1)%ringLength;
        float delayed[2]{};
        for(int c=0;c<chans;++c){
            outputDelay[(size_t)c][(size_t)delayWrite]=result[c];
            delayed[c]=outputDelay[(size_t)c][(size_t)readIndex];
        }
        delayWrite=readIndex;
        if (limiting) {
            // Only an explicitly active limiter may alter peaks. No hidden
            // ceiling on the floating-point output when the limiter is absent.
            const float ceiling=limiter->limCeiling;
            for(int c=0;c<chans;++c)delayed[c]*=limiterEnvelope;
            const float safeguard=mixpilot::linkedSamplePeakGuard(delayed,chans,true,ceiling);
            const float release=std::exp(-1.0f/(float)(sampleRate*0.12));
            if(safeguard<safetyGain)safetyGain=safeguard;
            else safetyGain=release*safetyGain+(1.0f-release)*safeguard;
            const float gain=limiterEnvelope*safetyGain;
            for(int c=0;c<chans;++c){
                const float protectedSample=juce::jlimit(-ceiling,ceiling,delayed[c]*safetyGain);
                b.getWritePointer(c)[n]=protectedSample;
                out=juce::jmax(out,std::abs(protectedSample));
            }
            const float applied=juce::jmax(0.0f,-db(juce::jmax(1.0e-9f,gain)));
            blockLimiterGr=juce::jmax(blockLimiterGr,applied);
            gr=juce::jmax(gr,applied);
        } else {
            safetyGain=1.0f;
            for(int c=0;c<chans;++c){
                // Preserve the delayed sample exactly, even above 0 dBFS.
                // The fixed 5 ms latency remains correctly reported to the host.
                b.getWritePointer(c)[n]=delayed[c];
                out=juce::jmax(out,std::abs(delayed[c]));
            }
        }
        if(capturing && frames+n<targetFrames) {
            float processedMono=0.0f;
            for(int c=0;c<chans;++c)
                processedMono+=b.getWritePointer(c)[n]/(float)chans;
            liveCaptureProcessed[(size_t)(frames+n)]=processedMono;
        }
    }
    for(int i=0;i<slots;++i)
        limiterMeters[(size_t)i].store(i==limiterSlot?blockLimiterGr:0.0f,std::memory_order_relaxed);
    // Audio-thread measured snapshots; the GUI only loads atomics.
    for(int i=0;i<slots;++i){
        if(settings[(size_t)i].enabled && (settings[(size_t)i].type==4 || settings[(size_t)i].type==13)){
            compressorMeters[(size_t)i].store(blockCompressorGr[(size_t)i],std::memory_order_relaxed);
            compressorDetectors[(size_t)i].store(blockDetectorDb[(size_t)i],std::memory_order_relaxed);
        }else{
            compressorMeters[(size_t)i].store(0.0f,std::memory_order_relaxed);
            compressorDetectors[(size_t)i].store(-90.0f,std::memory_order_relaxed);
        }
    }
    for(int i=0;i<slots;++i){
        if(settings[(size_t)i].enabled && settings[(size_t)i].type==5){
            deEsserMeters[(size_t)i].store(blockDeEsserGr[(size_t)i],std::memory_order_relaxed);
            deEsserDetectors[(size_t)i].store(blockDeEsserDb[(size_t)i],std::memory_order_relaxed);
        }else{
            deEsserMeters[(size_t)i].store(0.0f,std::memory_order_relaxed);
            deEsserDetectors[(size_t)i].store(-90.0f,std::memory_order_relaxed);
        }
    }
    // Publish once per block, never per sample. Disabled bands publish zero
    // immediately; changes in the actual DSP wet state drive the animation.
    for(int i=0;i<slots;++i){
        for(int band=0;band<eqBands;++band){
            float depth=0.0f;
            if(dynamicSettings[(size_t)i][(size_t)band].enabled){
                for(int c=0;c<chans;++c)
                    depth+=stage[(size_t)c][(size_t)i].dynamicEQ[(size_t)band].wet;
                depth/=static_cast<float>(chans);
            }
            dynamicMeter[(size_t)i][(size_t)band].store(
                juce::jlimit(0.0f,1.0f,depth),std::memory_order_relaxed);
        }
    }
    inLevel.store(in);outLevel.store(out);reduction.store(gr);
    if(capturing){
        frames+=count;
        if(frames>=targetFrames){
            const double samples=(double)juce::jmax<int64_t>(1,frames*chans);
            auto fromEnergy=[&](double e){return db((float)std::sqrt(e/samples));};
            measured[0].store(fromEnergy(rmsSum));measured[1].store(fromEnergy(lowSum));
            measured[2].store(fromEnergy(midSum));measured[3].store(fromEnergy(highSum));
            measured[4].store(db(peak));
            measured[5].store(juce::jlimit(0.0f,90.0f,db(peak)-fromEnergy(rmsSum)));
            const bool stereoOk=captureStereo && leftEnergy>1.0e-12 && rightEnergy>1.0e-12;
            measured[6].store(stereoOk ? (float)juce::jlimit(-1.0,1.0,
                crossEnergy/std::sqrt(leftEnergy*rightEnergy)) : 0.0f);
            measured[7].store(stereoOk ? (float)juce::jlimit(-60.0,60.0,
                10.0*std::log10(leftEnergy/rightEnergy)) : 0.0f);
            measured[8].store(minimumWindowEnergy<1.0e29 ?
                db((float)std::sqrt(minimumWindowEnergy)) : -90.0f);
            measuredStereoValid.store(stereoOk,std::memory_order_relaxed);
            std::array<float,diagnosticBands> bands{};
            for(int k=0;k<diagnosticBands;++k)bands[(size_t)k]=fromEnergy(diagnosticSum[(size_t)k]);
            float best=0,hz=0;
            for(int k=1;k<diagnosticBands-1;++k){
                const float prominence=bands[(size_t)k]-.5f*(bands[(size_t)(k-1)]+bands[(size_t)(k+1)]);
                if(prominence>best&&bands[(size_t)k]>-70){best=prominence;hz=diagnosticHz[k];}
            }
            resonanceHz.store(best>=2?hz:0);resonanceDb.store(best);
            capturing=false;captured.store(true);
        }
    }
}
void MixPilotProcessor::setFileFeatures(ToneFeatures f) noexcept {
    fileValid.store(false);
    fileMeasured[0].store(f.rmsDb);fileMeasured[1].store(f.lowDb);fileMeasured[2].store(f.midDb);
    fileMeasured[3].store(f.highDb);fileMeasured[4].store(f.peakDb);
    fileMeasured[5].store(f.crestDb);fileMeasured[6].store(f.stereoCorrelation);
    fileMeasured[7].store(f.stereoBalanceDb);fileMeasured[8].store(f.quietestWindowDb);
    fileStereoValid.store(f.stereoValid,std::memory_order_relaxed);
    fileValid.store(f.valid);
}
ToneFeatures MixPilotProcessor::features() const noexcept {
    ToneFeatures f;
    if(!fileValid.load()&&!captured.load())return f;
    auto& source=fileValid.load()?fileMeasured:measured;
    f.rmsDb=source[0].load();f.lowDb=source[1].load();f.midDb=source[2].load();
    f.highDb=source[3].load();f.peakDb=source[4].load();
    f.crestDb=source[5].load();f.stereoCorrelation=source[6].load();
    f.stereoBalanceDb=source[7].load();f.quietestWindowDb=source[8].load();
    f.stereoValid=fileValid.load()?fileStereoValid.load():measuredStereoValid.load();
    f.valid=true;return f;
}
void MixPilotProcessor::spectrum(std::array<float,spectrumSize>& out) const noexcept {
    const unsigned int end=ringWrite.load(std::memory_order_relaxed);
    for(int k=0;k<spectrumSize;++k)out[(size_t)k]=ring[(size_t)((end+k)%spectrumSize)].load(std::memory_order_relaxed);
}
void MixPilotProcessor::setPrompt(juce::String s){juce::ScopedLock l(stateLock);promptText=std::move(s);}
juce::String MixPilotProcessor::prompt() const {juce::ScopedLock l(stateLock);return promptText;}
void MixPilotProcessor::setReference(ToneFeatures f){juce::ScopedLock l(stateLock);referenceFeatures=f;}
ToneFeatures MixPilotProcessor::reference() const {juce::ScopedLock l(stateLock);return referenceFeatures;}
void MixPilotProcessor::setSpectralProfile(bool isReference, SpectralProfile profile){
    juce::ScopedLock l(stateLock);
    (isReference?referenceSpectralFeatures:sourceSpectralFeatures)=profile;
}
SpectralProfile MixPilotProcessor::spectralProfile(bool isReference) const {
    juce::ScopedLock l(stateLock);
    return isReference?referenceSpectralFeatures:sourceSpectralFeatures;
}
void MixPilotProcessor::setModelPreference(juce::String model){
    juce::ScopedLock l(stateLock);
    modelPreferenceText=model.trim().isEmpty()?"AUTO":model.trim();
}
juce::String MixPilotProcessor::modelPreference() const {
    juce::ScopedLock l(stateLock);
    return modelPreferenceText;
}
void MixPilotProcessor::getStateInformation(juce::MemoryBlock& out){
    auto s=apvts.copyState();
    {juce::ScopedLock l(stateLock);
        s.setProperty("prompt",promptText,nullptr);
        s.setProperty("referenceValid",referenceFeatures.valid,nullptr);
        s.setProperty("refRms",referenceFeatures.rmsDb,nullptr);
        s.setProperty("refLow",referenceFeatures.lowDb,nullptr);
        s.setProperty("refMid",referenceFeatures.midDb,nullptr);
        s.setProperty("refHigh",referenceFeatures.highDb,nullptr);
        s.setProperty("refPeak",referenceFeatures.peakDb,nullptr);
        s.setProperty("refCrest",referenceFeatures.crestDb,nullptr);
        s.setProperty("refCorrelation",referenceFeatures.stereoCorrelation,nullptr);
        s.setProperty("refBalance",referenceFeatures.stereoBalanceDb,nullptr);
        s.setProperty("refQuietest",referenceFeatures.quietestWindowDb,nullptr);
        s.setProperty("refStereoValid",referenceFeatures.stereoValid,nullptr);
        s.setProperty("modelPreference",modelPreferenceText,nullptr);
        // The 8.9.5 8192-point spectrum is not comparable with older
        // 2048-point saved profiles. Invalidate old profiles upon load.
        s.setProperty("spectralAlgorithmVersion",2,nullptr);
        s.setProperty("sourceSpectrumValid",sourceSpectralFeatures.valid,nullptr);
        s.setProperty("referenceSpectrumValid",referenceSpectralFeatures.valid,nullptr);
        for(int k=0;k<SpectralProfile::bands;++k){
            const auto suffix=juce::String(k);
            s.setProperty(juce::Identifier("sourceSpectrum"+suffix),sourceSpectralFeatures.relativeDb[(size_t)k],nullptr);
            s.setProperty(juce::Identifier("referenceSpectrum"+suffix),referenceSpectralFeatures.relativeDb[(size_t)k],nullptr);
        }
    }
    // File-based source measurements are part of the saved mixing context.
    if(fileValid.load(std::memory_order_acquire)){
        const auto source=features();
        s.setProperty("fileSourceValid",true,nullptr);
        s.setProperty("fileSourceRms",source.rmsDb,nullptr);
        s.setProperty("fileSourceLow",source.lowDb,nullptr);
        s.setProperty("fileSourceMid",source.midDb,nullptr);
        s.setProperty("fileSourceHigh",source.highDb,nullptr);
        s.setProperty("fileSourcePeak",source.peakDb,nullptr);
        s.setProperty("fileSourceCrest",source.crestDb,nullptr);
        s.setProperty("fileSourceCorrelation",source.stereoCorrelation,nullptr);
        s.setProperty("fileSourceBalance",source.stereoBalanceDb,nullptr);
        s.setProperty("fileSourceQuietest",source.quietestWindowDb,nullptr);
        s.setProperty("fileSourceStereoValid",source.stereoValid,nullptr);
    }else s.setProperty("fileSourceValid",false,nullptr);
    if(auto xml=s.createXml())copyXmlToBinary(*xml,out);
}
void MixPilotProcessor::setStateInformation(const void* data,int length){
    if(auto xml=getXmlFromBinary(data,length)){
        auto s=juce::ValueTree::fromXml(*xml);
        if(s.isValid()&&s.hasType(apvts.state.getType())){
            {juce::ScopedLock l(stateLock);
                promptText=s.getProperty("prompt","").toString();
                referenceFeatures.valid=(bool)s.getProperty("referenceValid",false);
                referenceFeatures.rmsDb=(float)s.getProperty("refRms",-90.0f);
                referenceFeatures.lowDb=(float)s.getProperty("refLow",-90.0f);
                referenceFeatures.midDb=(float)s.getProperty("refMid",-90.0f);
                referenceFeatures.highDb=(float)s.getProperty("refHigh",-90.0f);
                referenceFeatures.peakDb=(float)s.getProperty("refPeak",-90.0f);
                referenceFeatures.crestDb=(float)s.getProperty("refCrest",0.0f);
                referenceFeatures.stereoCorrelation=(float)s.getProperty("refCorrelation",0.0f);
                referenceFeatures.stereoBalanceDb=(float)s.getProperty("refBalance",0.0f);
                referenceFeatures.quietestWindowDb=(float)s.getProperty("refQuietest",-90.0f);
                referenceFeatures.stereoValid=(bool)s.getProperty("refStereoValid",false);
                modelPreferenceText=s.getProperty("modelPreference","AUTO").toString();
                if(modelPreferenceText.trim().isEmpty())modelPreferenceText="AUTO";
                const bool compatibleSpectrum=(int)s.getProperty("spectralAlgorithmVersion",0)==2;
                sourceSpectralFeatures.valid=compatibleSpectrum && (bool)s.getProperty("sourceSpectrumValid",false);
                referenceSpectralFeatures.valid=compatibleSpectrum && (bool)s.getProperty("referenceSpectrumValid",false);
                for(int k=0;k<SpectralProfile::bands;++k){
                    const auto suffix=juce::String(k);
                    sourceSpectralFeatures.relativeDb[(size_t)k]=(float)s.getProperty(juce::Identifier("sourceSpectrum"+suffix),0.0f);
                    referenceSpectralFeatures.relativeDb[(size_t)k]=(float)s.getProperty(juce::Identifier("referenceSpectrum"+suffix),0.0f);
                }
            }
            if((bool)s.getProperty("fileSourceValid",false)){
                ToneFeatures f;
                f.valid=true;
                f.rmsDb=(float)s.getProperty("fileSourceRms",-90.0f);
                f.lowDb=(float)s.getProperty("fileSourceLow",-90.0f);
                f.midDb=(float)s.getProperty("fileSourceMid",-90.0f);
                f.highDb=(float)s.getProperty("fileSourceHigh",-90.0f);
                f.peakDb=(float)s.getProperty("fileSourcePeak",-90.0f);
                f.crestDb=(float)s.getProperty("fileSourceCrest",0.0f);
                f.stereoCorrelation=(float)s.getProperty("fileSourceCorrelation",0.0f);
                f.stereoBalanceDb=(float)s.getProperty("fileSourceBalance",0.0f);
                f.quietestWindowDb=(float)s.getProperty("fileSourceQuietest",-90.0f);
                f.stereoValid=(bool)s.getProperty("fileSourceStereoValid",false);
                setFileFeatures(f);
            }else{
                fileValid.store(false,std::memory_order_release);
            }
            apvts.replaceState(s);filterCountdown=0;
        }
    }
}
juce::AudioProcessorEditor* MixPilotProcessor::createEditor(){return new MixPilotEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MixPilotProcessor();}

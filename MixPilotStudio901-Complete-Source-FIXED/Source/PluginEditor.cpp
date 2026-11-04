#include "AI/ReferenceMetrics.h"
#include "AI/VocalChainPlanner.h"
#include "PluginEditor.h"
#include "SpectralProfiler.h"
#include "ReferenceEQ.h"
#include <cmath>
#include <complex>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <utility>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <winhttp.h>
#endif
namespace {
const auto bg=juce::Colour(0xff080c14), surface=juce::Colour(0xff101924),panel=juce::Colour(0xff162433);
const auto border=juce::Colour(0xff2a3a4c),uiText=juce::Colour(0xffedf6ff),muted=juce::Colour(0xff91a6ba);
const auto cyan=juce::Colour(0xff4ce5e8),purple=juce::Colour(0xffb282f7),magenta=juce::Colour(0xfff16cc0);
const auto green=juce::Colour(0xff86e8b2);
float dB(float x){return juce::Decibels::gainToDecibels(juce::jmax(x,1.e-9f));}
float param(MixPilotProcessor& p,int s,const char* key){return p.value("s"+juce::String(s+1)+key);}
juce::String key(int s,const char* what){return "s"+juce::String(s+1)+what;}
void label(juce::Label& l,const juce::String& t,float size=12,juce::Colour colour=uiText,bool bold=false){
    l.setText(t,juce::dontSendNotification);
    l.setFont(juce::Font(juce::FontOptions(size,bold?juce::Font::bold:juce::Font::plain)));
    l.setColour(juce::Label::textColourId,colour);
}
void button(juce::TextButton& b,bool primary=false){
    b.setColour(juce::TextButton::buttonColourId,primary?cyan:panel);
    b.setColour(juce::TextButton::textColourOffId,primary?bg:uiText);
}
void slider(juce::Slider& s){
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight,false,73,25);
    s.setColour(juce::Slider::trackColourId,cyan);
    s.setColour(juce::Slider::thumbColourId,cyan);
    s.setColour(juce::Slider::backgroundColourId,border);
    s.setColour(juce::Slider::textBoxTextColourId,uiText);
    s.setColour(juce::Slider::textBoxOutlineColourId,border);
    s.setColour(juce::Slider::textBoxBackgroundColourId,surface);
}
float normalizedFreq(float f){return std::log(juce::jlimit(20.0f,20000.0f,f)/20.0f)/std::log(1000.0f);}
}
Visualizer::Visualizer(MixPilotEditor& p):owner(p){}
juce::Rectangle<float> Visualizer::plottingRect() const {return getLocalBounds().toFloat().reduced(55,37);}
void Visualizer::paint(juce::Graphics& g){
    g.fillAll(surface);
    auto r=plottingRect();
    const int selectedType=(int)param(owner.processor,owner.selectedSlot,"type");
    if(selectedType==4 || selectedType==13){
        auto plot=plottingRect();
        g.setColour(juce::Colour(0xff15202a));g.fillRoundedRectangle(plot,6.0f);
        g.setFont(12.0f);
        for(int d=-60;d<=0;d+=10){
            const float v=(d+60)/60.0f;
            const float x=plot.getX()+v*plot.getWidth();
            const float y=plot.getBottom()-v*plot.getHeight();
            g.setColour(border);g.drawVerticalLine((int)x,plot.getY(),plot.getBottom());
            g.drawHorizontalLine((int)y,plot.getX(),plot.getRight());
            g.setColour(muted);
            g.drawText(juce::String(d),(int)x-16,(int)plot.getBottom()+5,34,18,juce::Justification::centred);
        }
        const float amount=param(owner.processor,owner.selectedSlot,"amount")*.01f;
        const float threshold=selectedType==4?-6.0f-42.0f*amount:-9.0f-33.0f*amount;
        const float ratio=selectedType==4?juce::jmax(1.0f,param(owner.processor,owner.selectedSlot,"ratio")):
            juce::jlimit(1.5f,5.0f,param(owner.processor,owner.selectedSlot,"ratio"));
        juce::Path unity,compression;
        for(int i=0;i<=240;++i){
            const float input=-60.0f+i*.25f;
            const float output=input-juce::jmax(0.0f,input-threshold)*(1.0f-1.0f/ratio);
            const float x=plot.getX()+i/240.0f*plot.getWidth();
            const float yy=plot.getBottom()-(output+60.0f)/60.0f*plot.getHeight();
            const float uy=plot.getBottom()-(input+60.0f)/60.0f*plot.getHeight();
            if(i==0){unity.startNewSubPath(x,uy);compression.startNewSubPath(x,yy);}
            else {unity.lineTo(x,uy);compression.lineTo(x,yy);}
        }
        g.setColour(cyan.withAlpha(.45f));g.strokePath(unity,juce::PathStrokeType(1.8f));
        g.setColour(juce::Colour(0xffd4ac75));g.strokePath(compression,juce::PathStrokeType(3.0f));
        const float detector=owner.processor.compressorDetector(owner.selectedSlot);
        const float reduction=owner.processor.compressorReduction(owner.selectedSlot);
        const float dx=plot.getX()+juce::jlimit(0.0f,1.0f,(detector+60.0f)/60.0f)*plot.getWidth();
        const float dy=plot.getBottom()-juce::jlimit(0.0f,1.0f,(detector-reduction+60.0f)/60.0f)*plot.getHeight();
        g.setColour(purple.withAlpha(.16f));g.fillEllipse(dx-15,dy-15,30,30);
        g.setColour(purple);g.fillEllipse(dx-5,dy-5,10,10);
        g.setFont(14.0f);g.setColour(uiText);
        g.drawText(selectedType==4?"FET 76  /  PEAK COMPRESSION":"OPTO LEVELER  /  RMS COMPRESSION",
            (int)plot.getX()+14,(int)plot.getY()+9,360,24,juce::Justification::left);
        g.setColour(juce::Colour(0xffd4ac75));
        g.drawText("LIVE GR  -"+juce::String(reduction,1)+" dB",(int)plot.getRight()-220,
            (int)plot.getY()+9,205,24,juce::Justification::centredRight);
        g.setColour(muted);g.setFont(11.0f);
        g.drawText("INPUT LEVEL (dBFS)",(int)plot.getCentreX()-85,(int)plot.getBottom()+21,170,18,juce::Justification::centred);
        return;
    }

    if(selectedType==10){
        // A missing per-slot limiter parameter usually means the editor and
        // processor came from different versions of the project.
        if(owner.processor.apvts.getParameter(key(owner.selectedSlot,"limCeiling"))==nullptr ||
           owner.processor.apvts.getParameter(key(owner.selectedSlot,"limTruePeak"))==nullptr){
            g.setColour(juce::Colours::orange);
            g.setFont(17.0f);
            g.drawFittedText("LIMITER PARAMETERS MISSING - REPLACE PROCESSOR.CPP AND PROCESSOR.H",
                             getLocalBounds().reduced(18),juce::Justification::centred,3);
            return;
        }
        // A minimal measurement-based view until the full UI redesign.
        auto plot=plottingRect();
        g.setColour(juce::Colour(0xff15202a));g.fillRoundedRectangle(plot,6.0f);
        for(int db=-60;db<=0;db+=12){
            const float y=plot.getBottom()-(db+60.0f)*plot.getHeight()/60.0f;
            g.setColour(border);g.drawHorizontalLine((int)y,plot.getX(),plot.getRight());
        }
        const float ceiling=param(owner.processor,owner.selectedSlot,"limCeiling");
        const float inputDrive=param(owner.processor,owner.selectedSlot,"limInput");
        const float reduction=owner.processor.limiterReduction(owner.selectedSlot);
        const float cy=plot.getBottom()-juce::jlimit(0.0f,1.0f,(ceiling+60.0f)/60.0f)*plot.getHeight();
        g.setColour(cyan);g.drawHorizontalLine((int)cy,plot.getX(),plot.getRight());
        g.setColour(purple);g.setFont(14.0f);
        g.drawText("LOOKAHEAD LIMITER  /  5ms  /  4x PEAK ESTIMATE",
            (int)plot.getX()+14,(int)plot.getY()+14,540,24,juce::Justification::left);
        g.setColour(uiText);g.setFont(18.0f);
        g.drawText("LIVE GAIN REDUCTION  -"+juce::String(reduction,1)+" dB",
            (int)plot.getX()+14,(int)plot.getY()+47,550,32,juce::Justification::left);
        if(reduction<0.05f){
            g.setColour(muted);g.setFont(13.0f);
            g.drawText("No gain reduction detected - raise limiter input or lower ceiling to test",
                (int)plot.getX()+14,(int)plot.getY()+142,(int)plot.getWidth()-28,26,
                juce::Justification::left);
        }
        g.setColour(muted);g.setFont(13.0f);
        g.drawText("DRIVE  "+juce::String(inputDrive,1)+" dB    CEILING  "+juce::String(ceiling,1)+
            " dBTP    RELEASE  "+juce::String(param(owner.processor,owner.selectedSlot,"limRelease"),0)+
            " ms    TRUE PEAK  "+juce::String(param(owner.processor,owner.selectedSlot,"limTruePeak")>.5f?"ON":"OFF"),
            (int)plot.getX()+14,(int)plot.getY()+88,(int)plot.getWidth()-28,28,juce::Justification::left);
        return;
    }

    if(selectedType==5){
        auto plot=plottingRect();
        g.setColour(juce::Colour(0xff15202a));g.fillRoundedRectangle(plot,6.0f);
        for(int db=-60;db<=0;db+=10){
            const float y=plot.getBottom()-(db+60.0f)/60.0f*plot.getHeight();
            g.setColour(border);g.drawHorizontalLine((int)y,plot.getX(),plot.getRight());
            g.setColour(muted);g.drawText(juce::String(db)+" dBFS",(int)plot.getX()-53,(int)y-8,47,16,juce::Justification::centredRight);
        }
        const float threshold=param(owner.processor,owner.selectedSlot,"deessThreshold");
        const float detector=owner.processor.deEsserDetector(owner.selectedSlot);
        const float reduction=owner.processor.deEsserReduction(owner.selectedSlot);
        const float ty=plot.getBottom()-(threshold+60.0f)/60.0f*plot.getHeight();
        const float dy=plot.getBottom()-juce::jlimit(0.0f,1.0f,(detector+60.0f)/60.0f)*plot.getHeight();
        g.setColour(cyan.withAlpha(.75f));g.drawHorizontalLine((int)ty,plot.getX(),plot.getRight());
        g.setColour(purple);g.fillEllipse(plot.getCentreX()-8,dy-8,16,16);
        g.setColour(uiText);g.setFont(14.0f);
        g.drawText("DE-ESSER  /  "+juce::String((int)param(owner.processor,owner.selectedSlot,"freq"))+" Hz",
            (int)plot.getX()+14,(int)plot.getY()+8,330,24,juce::Justification::left);
        g.setColour(juce::Colour(0xffd4ac75));
        g.drawText("LIVE REDUCTION  -"+juce::String(reduction,1)+" dB",
            (int)plot.getRight()-285,(int)plot.getY()+8,270,24,juce::Justification::centredRight);
        g.setColour(muted);g.setFont(11.0f);
        g.drawText("CYAN = THRESHOLD     PURPLE = SIBILANCE DETECTOR     MODE: "+
            juce::String(param(owner.processor,owner.selectedSlot,"deessMode")>.5f?"WIDEBAND":"SPLIT BAND"),
            (int)plot.getX()+12,(int)plot.getBottom()+20,(int)plot.getWidth()-24,18,juce::Justification::centred);
        return;
    }

    const float freqMarks[]{20,50,100,200,500,1000,2000,5000,10000,20000};
    const char* freqLabels[]{"20","50","100","200","500","1k","2k","5k","10k","20k"};
    g.setFont(12);
    for(int i=0;i<10;++i){
        const float x=r.getX()+normalizedFreq(freqMarks[i])*r.getWidth();
        g.setColour(border);g.drawVerticalLine((int)x,r.getY(),r.getBottom());
        g.setColour(muted);g.drawText(freqLabels[i],(int)x-20,(int)r.getBottom()+9,40,17,juce::Justification::centred);
    }
    for(int db=-18;db<=18;db+=6){
        const float y=r.getCentreY()-db*r.getHeight()/36;
        g.setColour(db==0?border.brighter(.45f):border);g.drawHorizontalLine((int)y,r.getX(),r.getRight());
        g.setColour(muted);g.drawText(juce::String(db)+" dB",3,(int)y-8,48,16,juce::Justification::centredRight);
    }
    juce::Path spectrumPath;
    for(int i=0;i<128;++i){
        const float f=20*std::pow(1000.0f,i/127.0f);
        const float x=r.getX()+normalizedFreq(f)*r.getWidth();
        const float y=juce::jmap(juce::jlimit(-88.0f,-15.0f,spectrum[(size_t)i]),-88.0f,-15.0f,r.getBottom(),r.getY());
        if(i==0)spectrumPath.startNewSubPath(x,y);else spectrumPath.lineTo(x,y);
    }
    g.setColour(cyan.withAlpha(.29f));g.strokePath(spectrumPath,juce::PathStrokeType(2.0f));
    // The large graph deliberately describes ONLY the selected module.
    // A whole-rack response was confusing because other HPF/LPF modules
    // modified the curve while the user was editing one EQ.
    struct FilterResponse { juce::IIRCoefficients coefficients; int repetitions=1; };
    std::vector<FilterResponse> responses;
    const auto rate=owner.processor.sampleRateHz();
    const int slot=owner.selectedSlot;
    const int moduleType=(int)param(owner.processor,slot,"type");
    const bool moduleEnabled=param(owner.processor,slot,"enable")>.5f;
    auto bandStem=[slot](int band){return key(slot,"b")+juce::String(band+1);};
    auto bandValue=[&](int band,const char* suffix)->float{
        if(band==0){
            if(juce::String(suffix)=="f")return param(owner.processor,slot,"freq");
            if(juce::String(suffix)=="g")return param(owner.processor,slot,"gain");
            if(juce::String(suffix)=="q")return param(owner.processor,slot,"q");
            if(juce::String(suffix)=="type")return param(owner.processor,slot,"eqtype");
            return param(owner.processor,slot,"eqon");
        }
        const auto stem=bandStem(band);
        return owner.processor.value(stem+suffix);
    };
    const bool isEQ=moduleType==1;
    if(moduleEnabled && isEQ){
        for(int band=0;band<MixPilotProcessor::eqBands;++band){
            if(bandValue(band,"on")<.5f)continue;
            responses.push_back({MixPilotProcessor::bandCoefficients(rate,
                (int)bandValue(band,"type"),bandValue(band,"f"),
                bandValue(band,"q"),bandValue(band,"g")),1});
        }
    }else if(moduleEnabled && (moduleType==2||moduleType==3)){
        const auto freq=param(owner.processor,slot,"freq");
        const auto cf=moduleType==2
            ? juce::IIRCoefficients::makeHighPass(rate,juce::jlimit(20.0,rate*.44,(double)freq))
            : juce::IIRCoefficients::makeLowPass(rate,juce::jlimit(20.0,rate*.44,(double)freq));
        responses.push_back({cf,(int)param(owner.processor,slot,"slope")==1?2:1});
    }
    // Freeze the audio-thread dynamics wet amounts for this GUI frame.
    // The colourFilter coefficients and the interpolation formula below match
    // processBlock: output = input + wet * (filtered - input).
    struct DynamicPlotBand {
        juce::IIRCoefficients coefficients;
        float wet=0.0f;
    };
    std::array<DynamicPlotBand,MixPilotProcessor::eqBands> dynamicPlot;
    bool dynamicActive=false;
    if(isEQ && moduleEnabled){
        for(int band=0;band<MixPilotProcessor::eqBands;++band){
            const auto stem=bandStem(band);
            const int type=(int)bandValue(band,"type");
            if(bandValue(band,"on")<.5f || type>2 || owner.processor.value(stem+"dynon")<.5f)
                continue;
            dynamicActive=true;
            auto& d=dynamicPlot[(size_t)band];
            d.coefficients=MixPilotProcessor::bandCoefficients(rate,type,
                bandValue(band,"f"),bandValue(band,"q"),owner.processor.value(stem+"dynrange"));
            d.wet=owner.processor.dynamicDepth(slot,band);
        }
    }
    auto complexResponse=[rate](const juce::IIRCoefficients& coefficients,float hz)->std::complex<double>{
        const auto w=2.0*juce::MathConstants<double>::pi*juce::jmin((double)hz,rate*.49)/rate;
        const std::complex<double> z1(std::cos(w),-std::sin(w)),z2=z1*z1;
        const auto* a=coefficients.coefficients;
        const std::complex<double> numerator=(double)a[0]+(double)a[1]*z1+(double)a[2]*z2;
        const std::complex<double> denominator=1.0+(double)a[3]*z1+(double)a[4]*z2;
        return numerator/denominator;
    };
    juce::Path staticCurve,liveCurve;
    for(int col=0;col<=420;++col){
        const float f=20*std::pow(1000.0f,col/420.0f);
        double staticDb=0.0;
        for(const auto& filter:responses){
            const auto magnitude=std::abs(complexResponse(filter.coefficients,f));
            staticDb+=20.0*std::log10(juce::jmax(1.0e-15,magnitude))*filter.repetitions;
        }
        double liveDb=staticDb;
        if(dynamicActive){
            for(const auto& dyn:dynamicPlot){
                if(dyn.wet<=0.0f)continue;
                const auto h=complexResponse(dyn.coefficients,f);
                const std::complex<double> mixed=1.0+(double)dyn.wet*(h-1.0);
                liveDb+=20.0*std::log10(juce::jmax(1.0e-15,std::abs(mixed)));
            }
        }
        const float x=r.getX()+col*r.getWidth()/420;
        const float baseY=r.getCentreY()-juce::jlimit(-18.0f,18.0f,(float)staticDb)*r.getHeight()/36.0f;
        const float liveY=r.getCentreY()-juce::jlimit(-18.0f,18.0f,(float)liveDb)*r.getHeight()/36.0f;
        if(col==0){staticCurve.startNewSubPath(x,baseY);liveCurve.startNewSubPath(x,liveY);}
        else {staticCurve.lineTo(x,baseY);liveCurve.lineTo(x,liveY);}
    }
    // Cyan stays fixed. Purple follows the real per-band dynamic depth.
    g.setColour(cyan.withAlpha(.11f));g.strokePath(staticCurve,juce::PathStrokeType(7.0f));
    g.setColour(cyan);g.strokePath(staticCurve,juce::PathStrokeType(2.2f));
    if(dynamicActive){
        g.setColour(purple.withAlpha(.16f));g.strokePath(liveCurve,juce::PathStrokeType(8.0f));
        g.setColour(purple);g.strokePath(liveCurve,juce::PathStrokeType(2.5f));
    }
    // Only show editable nodes belonging to the current module.
    if(isEQ){
        for(int band=0;band<MixPilotProcessor::eqBands;++band){
            const bool active=bandValue(band,"on")>.5f && moduleEnabled;
            const float freq=bandValue(band,"f");
            const int filterType=(int)bandValue(band,"type");
            const float gain=filterType<=2?bandValue(band,"g"):0.0f;
            const float x=r.getX()+normalizedFreq(freq)*r.getWidth();
            const float y=r.getCentreY()-gain*r.getHeight()/36.0f;
            const bool selected=band==owner.selectedBand;
            const float radius=selected?10.0f:7.0f;
            if(selected){
                g.setColour(cyan.withAlpha(.18f));g.fillEllipse(x-17,y-17,34,34);
                g.setColour(cyan.withAlpha(.55f));g.drawEllipse(x-17,y-17,34,34,1.0f);
            }
            g.setColour(!active?muted.withAlpha(.4f):selected?cyan:juce::Colour(0xffdaa978));
            g.fillEllipse(x-radius,y-radius,2*radius,2*radius);
            if(selected||active){
                g.setColour(bg);g.setFont(selected?10.5f:9.0f);
                g.drawText(juce::String(band+1),(int)(x-radius-1),(int)(y-radius),
                           (int)(radius*2+2),(int)(radius*2),juce::Justification::centred);
            }
        }
    }else if(moduleType==2||moduleType==3){
        const float x=r.getX()+normalizedFreq(param(owner.processor,slot,"freq"))*r.getWidth();
        g.setColour(cyan);g.fillEllipse(x-7,r.getCentreY()-7,14,14);
    }
    // Legend: no synthetic dynamic motion. Purple appears only if dynamic
    // processing is enabled; it uses actual published DSP wet amounts.
    g.setFont(11.0f);
    g.setColour(cyan);g.fillRoundedRectangle(r.getRight()-188,r.getY()+8,21,3,1);
    g.setColour(uiText);g.drawText("STATIC EQ",(int)r.getRight()-162,(int)r.getY()+1,145,18,juce::Justification::left);
    if(dynamicActive){
        g.setColour(purple);g.fillRoundedRectangle(r.getRight()-188,r.getY()+27,21,3,1);
        g.setColour(uiText);g.drawText("LIVE DYNAMIC",(int)r.getRight()-162,(int)r.getY()+20,145,18,juce::Justification::left);
        const auto selectedStem=bandStem(owner.selectedBand);
        if(owner.processor.value(selectedStem+"dynon")>.5f){
            const float depth=owner.processor.dynamicDepth(slot,owner.selectedBand);
            const float range=owner.processor.value(selectedStem+"dynrange");
            const float approximate=range*depth;
            g.setColour(purple);
            g.drawText("BAND "+juce::String(owner.selectedBand+1)+"  "+juce::String(approximate,1)+" dB target",
                (int)r.getX()+12,(int)r.getY()+6,235,18,juce::Justification::left);
        }
    }
    const int chosen=(int)param(owner.processor,owner.selectedSlot,"type");
    if(chosen==4||chosen==6){
        // Transfer curve illustrating compression or saturation of selected module.
        juce::Rectangle<float> mini(r.getRight()-132,r.getY()+10,116,92);
        g.setColour(bg.withAlpha(.7f));g.fillRoundedRectangle(mini,6);
        g.setColour(muted);g.drawRect(mini);
        juce::Path p;
        for(int i=0;i<=80;++i){
            const float x=i/80.0f;
            float y=x;
            if(chosen==6){const float drive=1+param(owner.processor,owner.selectedSlot,"amount")*.12f;y=std::tanh(x*drive)/drive;}
            else {const float threshold=juce::jmap(param(owner.processor,owner.selectedSlot,"amount")*.01f,.78f,.2f);const float ratio=param(owner.processor,owner.selectedSlot,"ratio");if(x>threshold)y=threshold+(x-threshold)/ratio;}
            const float xx=mini.getX()+x*mini.getWidth(),yy=mini.getBottom()-y*mini.getHeight();
            if(i==0)p.startNewSubPath(xx,yy);else p.lineTo(xx,yy);
        }
        g.setColour(magenta);g.strokePath(p,juce::PathStrokeType(2));
    }
}
void Visualizer::mouseDown(const juce::MouseEvent& e){
    if((int)param(owner.processor,owner.selectedSlot,"type")==1){
        auto r=plottingRect();float nearest=1000000;int target=0;
        for(int b=0;b<MixPilotProcessor::eqBands;++b){
            const auto stem=key(owner.selectedSlot,"b")+juce::String(b+1);
            const float hz=b==0?param(owner.processor,owner.selectedSlot,"freq"):owner.processor.value(stem+"f");
            const float gain=b==0?param(owner.processor,owner.selectedSlot,"gain"):owner.processor.value(stem+"g");
            const float x=r.getX()+normalizedFreq(hz)*r.getWidth();
            const float y=r.getCentreY()-gain*r.getHeight()/36;
            const float dx=x-e.position.x,dy=y-e.position.y;
            const float distance=dx*dx+dy*dy;
            if(distance<nearest){nearest=distance;target=b;}
        }
        if(nearest<900.0f){owner.selectBand(target);drag(e);}
    }else drag(e);
}
void Visualizer::mouseDrag(const juce::MouseEvent& e){drag(e);}
void Visualizer::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& wheel){
    if((int)param(owner.processor,owner.selectedSlot,"type")!=1){
        juce::Component::mouseWheelMove(e,wheel);return;
    }
    const auto parameter=owner.selectedBand==0?key(owner.selectedSlot,"q"):
        key(owner.selectedSlot,"b")+juce::String(owner.selectedBand+1)+"q";
    const float previous=owner.processor.value(parameter);
    owner.processor.setValue(parameter,juce::jlimit(.3f,12.0f,previous+wheel.deltaY*1.2f));
    repaint();
}

void Visualizer::drag(const juce::MouseEvent& e){
    const int type=(int)param(owner.processor,owner.selectedSlot,"type");
    if(type<1||type>3)return;
    auto r=plottingRect();
    const float fx=juce::jlimit(0.0f,1.0f,(e.position.x-r.getX())/r.getWidth());
    const float f=20*std::pow(1000.0f,fx);
    const auto prefix=key(owner.selectedSlot,"b")+juce::String(owner.selectedBand+1);
    owner.processor.setValue(type==1&&owner.selectedBand>0?prefix+"f":key(owner.selectedSlot,"freq"),f);
    if(type==1){
        const auto bandType=(int)owner.processor.value(owner.selectedBand>0?prefix+"type":key(owner.selectedSlot,"eqtype"));
        if(bandType<=2)owner.processor.setValue(owner.selectedBand>0?prefix+"g":key(owner.selectedSlot,"gain"),juce::jlimit(-18.0f,18.0f,(r.getCentreY()-e.position.y)*36/r.getHeight()));
    }
    repaint();
}
MixPilotEditor::MixPilotEditor(MixPilotProcessor& p):AudioProcessorEditor(&p),processor(p),graph(*this){
    setSize(1540,950);
    label(title,"MIXPILOT  STUDIO",34,uiText,true);
    label(tagline,"VIRTUAL MIX RACK     /     MODULAR VOCAL ENGINE 8.9.6",12,muted);
    label(rackTitle,"SIGNAL CHAIN     /     SELECT A HARDWARE MODULE",16,uiText,true);
    label(graphTitle,"PRECISION EQ  /  12 BANDS  /  DYNAMIC CONTROL",16,uiText,true);
    label(detailTitle,"MODULE SETTINGS",15,uiText,true);
    label(status,"Ready. Analyze a vocal before generating an AI mix.",12,green);
    label(sourceStatus,"SOURCE  /  live / WAV",11,muted);
    label(referenceStatus,"REFERENCE  /  none",11,muted);
    label(meters,"IN  -100 dB      OUT  -100 dB      GR  0 dB",13,uiText);
    label(diagnostics,"Resonance scan: awaiting analysis",12,muted);
    label(sourceLabel,"TRACK TYPE",11,muted);label(micLabel,"MIC PROFILE",11,muted);
    label(slotLabel,"ADD / CHANGE MODULE",11,muted);
    label(modelLabel,"AI MODEL",11,muted);
    label(masterLabel,"OUTPUT",11,muted);label(wetLabel,"WET",11,muted);label(intensityLabel,"AI AMOUNT",11,muted);
    for(auto* l:{&title,&tagline,&rackTitle,&graphTitle,&detailTitle,&status,&sourceStatus,&referenceStatus,&meters,&diagnostics,&sourceLabel,&micLabel,&slotLabel,&modelLabel,&masterLabel,&wetLabel,&intensityLabel})addAndMakeVisible(*l);
    promptEditor.setMultiLine(true);promptEditor.setReturnKeyStartsNewLine(true);
    const auto stored=processor.prompt();
    promptEditor.setText(stored.isEmpty()?"Make my vocal sound polished and close to my loaded reference.":stored);
    promptEditor.setColour(juce::TextEditor::backgroundColourId,surface);
    promptEditor.setColour(juce::TextEditor::textColourId,uiText);
    promptEditor.setColour(juce::TextEditor::outlineColourId,border);
    promptEditor.onTextChange=[this]{processor.setPrompt(promptEditor.getText());};addAndMakeVisible(promptEditor);
    // AUTO uses the model advertised by LM Studio /v1/models.
    // Enter an exact API model id to override it without rebuilding the plugin.
    modelIdEditor.setText(processor.modelPreference(),juce::dontSendNotification);
    sourceSpectrum=processor.spectralProfile(false);
    referenceSpectrum=processor.spectralProfile(true);
    if(sourceSpectrum.valid)
        sourceStatus.setText("SOURCE  /  restored spectral analysis",juce::dontSendNotification);
    if(referenceSpectrum.valid)
        referenceStatus.setText("REFERENCE  /  restored spectral analysis",juce::dontSendNotification);
    else if(processor.reference().valid)
        referenceStatus.setText("REFERENCE  /  reload WAV for new analyzer",juce::dontSendNotification);
    modelIdEditor.setTooltip("AI model: AUTO discovers LM Studio models; enter an exact /v1/models id for manual selection.");
    modelIdEditor.setColour(juce::TextEditor::backgroundColourId,surface);
    modelIdEditor.setColour(juce::TextEditor::textColourId,uiText);
    modelIdEditor.setColour(juce::TextEditor::outlineColourId,border);
    modelIdEditor.onTextChange=[this]{processor.setModelPreference(modelIdEditor.getText());};
    addAndMakeVisible(modelIdEditor);
    for(auto* b:{&scan,&sourceButton,&referenceButton,&generate,&up,&down,&remove,&bypass}){button(*b,b==&generate);addAndMakeVisible(*b);}
    for(const auto& name:{"Universal","Vocals","Instrument","Drums","Stereo Mix"})source.addItem(name,source.getNumItems()+1);
    for(const auto& name:{"Unknown / Flat","Neumann TLM 103","Generic dynamic microphone"})mic.addItem(name,mic.getNumItems()+1);
    const auto types=MixPilotProcessor::typeNames();
    for(int i=0;i<types.size();++i)addModule.addItem(types[i],i+1);
    for(auto* cb:{&source,&mic,&addModule}){
        cb->setColour(juce::ComboBox::backgroundColourId,panel);
        cb->setColour(juce::ComboBox::textColourId,uiText);
        cb->setColour(juce::ComboBox::outlineColourId,border);
        addAndMakeVisible(*cb);
    }
    sourceAttach=std::make_unique<ComboAttach>(processor.apvts,"mode",source);
    micAttach=std::make_unique<ComboAttach>(processor.apvts,"mic",mic);
    addModule.onChange=[this]{processor.setSlotType(selectedSlot,addModule.getSelectedId()-1);updateControls();refreshRack();};
    addAndMakeVisible(rackViewport);
    rackViewport.setScrollBarsShown(false,true);
    rackViewport.setViewedComponent(&rackCanvas,false);
    rackCanvas.setSize(MixPilotProcessor::slots*161+18,244);
    for(int i=0;i<MixPilotProcessor::slots;++i){
        slots[i].configure(&processor,i);
        rackCanvas.addAndMakeVisible(slots[i]);
        slots[i].setBounds(7+i*161,5,151,233);
        slots[i].onClick=[this,i]{selectSlot(i);};
    }
    addAndMakeVisible(graph);
    const char* ids[]{"freq","gain","q","threshold","range","attack","release"};
    for(int i=0;i<7;++i){slider(controls[i]);controls[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);controls[i].setTextBoxStyle(juce::Slider::TextBoxBelow,false,96,25);controls[i].setLookAndFeel(&studioTheme);addAndMakeVisible(controls[i]);label(paramLabels[i],juce::String(ids[i]).toUpperCase(),11,muted);addAndMakeVisible(paramLabels[i]);}
    for(auto* s:{&master,&wet,&intensity}){slider(*s);addAndMakeVisible(*s);}
    masterAttach=std::make_unique<SliderAttach>(processor.apvts,"output",master);
    wetAttach=std::make_unique<SliderAttach>(processor.apvts,"wet",wet);
    intensityAttach=std::make_unique<SliderAttach>(processor.apvts,"intensity",intensity);
    scan.onClick=[this]{
        // Never reuse an older SOURCE WAV spectrum for a new live capture.
        // A new capture invalidates the old profile until six seconds of
        // unprocessed input have been measured using the shared FFT analyzer.
        sourceSpectrum=SpectralProfile{};
        processor.setSpectralProfile(false,sourceSpectrum);
        sourceStatus.setText("SOURCE  /  live analysis",juce::dontSendNotification);
        processor.startAnalysis();
        waitingForAnalysis=true;
        status.setText("Listening to six seconds of live input...",juce::dontSendNotification);
    };
    sourceButton.onClick=[this]{
        chooser=std::make_unique<juce::FileChooser>("Choose source WAV",juce::File(),"*.wav");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& fc){
            const auto file=fc.getResult();if(file.existsAsFile())status.setText(analyzeFile(file,false)?"Source analyzed - ready for AI.":"Unable to read source WAV.",juce::dontSendNotification);
        });
    };
    referenceButton.onClick=[this]{
        chooser=std::make_unique<juce::FileChooser>("Choose reference WAV",juce::File(),"*.wav");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& fc){
            const auto file=fc.getResult();if(file.existsAsFile())status.setText(analyzeFile(file,true)?"Reference measured. This is broad tonal matching only.":"Unable to read reference WAV.",juce::dontSendNotification);
        });
    };
    generate.onClick=[this]{requestMix();};
    up.onClick=[this]{if(selectedSlot>0){processor.swapSlots(selectedSlot,selectedSlot-1);selectSlot(selectedSlot-1);}};
    down.onClick=[this]{if(selectedSlot<MixPilotProcessor::slots-1){processor.swapSlots(selectedSlot,selectedSlot+1);selectSlot(selectedSlot+1);}};
    remove.onClick=[this]{processor.setSlotType(selectedSlot,0);updateControls();refreshRack();};
    bypass.onClick=[this]{processor.setValue(key(selectedSlot,"enable"),1-param(processor,selectedSlot,"enable"));refreshRack();};
    for(int i=0;i<MixPilotProcessor::eqBands;++i){
        bandSelectors[i].setButtonText(juce::String(i+1));
        button(bandSelectors[i],i==0);
        addAndMakeVisible(bandSelectors[i]);
        bandSelectors[i].onClick=[this,i]{selectBand(i);};
    }
    eqBandEnable.setButtonText("BAND ENABLED");button(eqBandEnable);addAndMakeVisible(eqBandEnable);
    button(eqDynamicEnable);addAndMakeVisible(eqDynamicEnable);
    eqDynamicEnable.onClick=[this]{
        const auto prefix=key(selectedSlot,"b")+juce::String(selectedBand+1)+"dyn";
        processor.setValue(prefix+"on",1.0f-processor.value(prefix+"on"));
        updateControls();graph.repaint();
    };
    label(eqFilterLabel,"FILTER SHAPE",11,muted);addAndMakeVisible(eqFilterLabel);
    const auto shapes=MixPilotProcessor::eqFilterNames();
    for(int i=0;i<shapes.size();++i)eqFilterType.addItem(shapes[i],i+1);
    eqFilterType.setColour(juce::ComboBox::backgroundColourId,panel);
    eqFilterType.setColour(juce::ComboBox::textColourId,uiText);
    eqFilterType.setColour(juce::ComboBox::outlineColourId,border);
    addAndMakeVisible(eqFilterType);
    eqBandEnable.onClick=[this]{
        const auto k=selectedBand==0?key(selectedSlot,"eqon"):
            key(selectedSlot,"b")+juce::String(selectedBand+1)+"on";
        processor.setValue(k,1.0f-processor.value(k));updateControls();graph.repaint();
    };
    selectSlot(1);
    startTimerHz(30);
}
MixPilotEditor::~MixPilotEditor(){stopTimer();chooser.reset();inferencePool.removeAllJobs(true,30000);rackViewport.setViewedComponent(nullptr,false);for(auto& c:controls)c.setLookAndFeel(nullptr);}
void MixPilotEditor::selectSlot(int i){
    selectedBand=0;
    selectedSlot=juce::jlimit(0,MixPilotProcessor::slots-1,i);
    addModule.onChange=nullptr;
    addModule.setSelectedId((int)param(processor,selectedSlot,"type")+1,juce::dontSendNotification);
    addModule.onChange=[this]{processor.setSlotType(selectedSlot,addModule.getSelectedId()-1);updateControls();refreshRack();};
    updateControls();refreshRack();graph.repaint();
}
void MixPilotEditor::selectBand(int i){
    selectedBand=juce::jlimit(0,MixPilotProcessor::eqBands-1,i);
    updateControls();graph.repaint();
}
void MixPilotEditor::updateControls(){
    // The same seven sliders serve processors with completely different ranges.
    // Remove old callbacks AND attachments before rebinding. Without an explicit
    // range reset a frequency knob (20..20000 Hz) can remain on the limiter.
    for(auto& control:controls)control.onValueChange=nullptr;
    for(auto& a:controlAttachments)a.reset();
    eqFilterAttach.reset();
    const int type=(int)param(processor,selectedSlot,"type");
    const char* names[7]{"freq","gain","q","amount","ratio","release","release"};
    if(type==4||type==13){names[0]="amount";names[1]="ratio";names[2]="attack";names[3]="release";}
    if(type==5){
        names[0]="freq";names[1]="deessThreshold";names[2]="deessRange";
        names[3]="attack";names[4]="release";names[5]="mix";names[6]="deessMode";
    }
    if(type==6){
        names[0]="satMode";names[1]="amount";names[2]="satTone";
        names[3]="satBias";names[4]="satOutput";names[5]="satQuality";names[6]="mix";
    }
    if(type==8){names[0]="amount";names[1]="mix";}
    if(type==10){names[0]="limInput";names[1]="limCeiling";
        names[2]="limRelease";names[3]="limTruePeak";}
    if(type==9){names[0]="delayMode";names[1]="delayMs";names[2]="delayFeedback";
        names[3]="delayTone";names[4]="delayDuck";names[5]="delaySync";names[6]="mix";}
    if(type==11){names[0]="spaceMode";names[1]="spaceDecay";names[2]="spacePredelay";
        names[3]="spaceDamp";names[4]="spaceLowCut";names[5]="spaceHighCut";names[6]="mix";}
    if(type==12){names[0]="freq";names[1]="amount";names[2]="attack";names[3]="ratio";}
    if(type==14||type==15){names[0]="modRate";names[1]="modDepth";
        names[2]="modFeedback";names[3]="modStereo";names[4]="mix";}
    if(type==7){names[0]="gain";names[1]="mix";}
    if(type==2||type==3){names[0]="freq";names[1]="slope";names[2]="mix";}
    int count=0;
    if(type==1){
        names[3]="threshold";names[4]="range";names[5]="attack";names[6]="release";
        count=7;
    }
    if(type==2||type==3)count=3;
    if(type==4||type==13)count=4;
    if(type==5)count=7;
    if(type==6)count=7;
    if(type==7||type==8)count=2;
    if(type==10)count=4;
    if(type==9||type==11)count=7;
    if(type==14||type==15)count=5;
    if(type==12)count=4;
    for(int k=0;k<7;++k){
        const bool visible=k<count;
        controls[k].setVisible(visible);paramLabels[k].setVisible(visible);
        if(!visible)continue;
        const juce::String name(names[k]);
        juce::String target=key(selectedSlot,names[k]);
        if(type==1 && k>=3){
            target=key(selectedSlot,"b")+juce::String(selectedBand+1)+"dyn"+names[k];
        }
        if(type==1 && selectedBand>0 && k<3){
            target=key(selectedSlot,"b")+juce::String(selectedBand+1)+(k==0?"f":k==1?"g":"q");
        }
        juce::String caption=name.toUpperCase();
        if(name=="freq")caption="FREQUENCY / CUTOFF (Hz)";
        if(name=="gain")caption="GAIN (dB)";
        if(name=="q")caption="Q / WIDTH";
        if(name=="amount")caption=(type==4||type==13)?"THRESHOLD DRIVE (%)":"STRENGTH (%)";
        if(name=="attack"||name=="release")caption=name.toUpperCase()+" (ms)";
        if(name=="slope")caption="SLOPE (0 = 12 dB, 1 = 24 dB)";
        if(name=="mix")caption="MODULE MIX (%)";
        if(name=="deessThreshold")caption="SIBILANCE THRESHOLD (dBFS)";
        if(name=="deessRange")caption="MAX SIBILANCE REDUCTION (dB)";
        if(name=="deessMode")caption="MODE (0 SPLIT / 1 WIDE)";
        if(name=="spaceMode")caption="REVERB (0 PLATE / 1 ROOM / 2 HALL)";
        if(name=="spaceDecay")caption="DECAY (SECONDS)";
        if(name=="spacePredelay")caption="PRE-DELAY (ms)";
        if(name=="spaceDamp")caption="DAMPING (%)";
        if(name=="spaceLowCut")caption="REVERB LOW CUT (Hz)";
        if(name=="spaceHighCut")caption="REVERB HIGH CUT (Hz)";
        if(name=="delayMode")caption="ECHO (0 STEREO / 1 PING PONG / 2 TAPE)";
        if(name=="delayMs")caption="DELAY TIME (ms)";
        if(name=="delaySync")caption="TEMPO SYNC (0 OFF / 1 ON)";
        if(name=="delayDivision")caption="SYNC (0 1/4 / 1 1/8 / 2 DOTTED / 3 1/16)";
        if(name=="delayFeedback")caption="FEEDBACK (%)";
        if(name=="delayTone")caption="DARKNESS (%)";
        if(name=="delayDuck")caption="DUCKING (%)";
        if(name=="modRate")caption="LFO RATE (Hz)";
        if(name=="modDepth")caption="MODULATION DEPTH (%)";
        if(name=="modFeedback")caption="FEEDBACK (%)";
        if(name=="modStereo")caption="STEREO OFFSET (DEGREES)";
        if(name=="limInput")caption="INPUT DRIVE (dB)";
        if(name=="limCeiling")caption="OUTPUT CEILING (dBTP TARGET)";
        if(name=="limRelease")caption="LIMITER RELEASE (ms)";
        if(name=="limTruePeak")caption="TRUE-PEAK ESTIMATOR (0 OFF / 1 ON)";
        if(name=="satMode")caption="CHARACTER (0 TUBE / 1 TAPE / 2 TRANSFORMER)";
        if(name=="satQuality")caption="OVERSAMPLING (0 1x / 1 2x / 2 4x)";
        if(name=="satTone")caption="TONE (-100 DARK / +100 BRIGHT)";
        if(name=="satBias")caption="BIAS / CHARACTER";
        if(name=="satOutput")caption="OUTPUT TRIM (dB)";
        if(name=="amount"&&type==6)caption="SATURATION DRIVE (%)";
        if(name=="threshold")caption="DYN THRESHOLD (dBFS)";
        if(name=="range")caption="DYN RANGE (dB)";
        label(paramLabels[k],caption,11,muted);
        // Always reset the knob range before binding it to a different processor.
        // JUCE attachments may update asynchronously; the control must never
        // display a stale frequency value as a limiter input gain.
        if(auto* attached=processor.apvts.getParameter(target)){
            const auto range=attached->getNormalisableRange();
            const double interval=range.interval>0.0f?(double)range.interval:0.01;
            controls[k].setRange((double)range.start,(double)range.end,interval);
            controls[k].setValue((double)attached->convertFrom0to1(attached->getValue()),
                                 juce::dontSendNotification);
        }
        // Every knob uses the same APVTS attachment path. The range is reset
        // above before attachment creation; attachments provide bidirectional
        // synchronization for knob dragging, typed values, host automation,
        // and restored preset state. No duplicate manual callback is installed.
        controlAttachments[(size_t)k]=std::make_unique<SliderAttach>(processor.apvts,target,controls[k]);
    }
    bypass.setButtonText(param(processor,selectedSlot,"enable")>.5f?"BYPASS OFF":"BYPASS ON");
    for(int b=0;b<MixPilotProcessor::eqBands;++b){
        bandSelectors[b].setVisible(type==1);
        bandSelectors[b].setColour(juce::TextButton::buttonColourId,b==selectedBand?cyan:panel);
        bandSelectors[b].setColour(juce::TextButton::textColourOffId,b==selectedBand?bg:uiText);
    }
    eqBandEnable.setVisible(type==1);
    eqDynamicEnable.setVisible(type==1);
    if(type==1){
        const int filterShape=(int)(selectedBand==0?processor.value(key(selectedSlot,"eqtype")):
            processor.value(key(selectedSlot,"b")+juce::String(selectedBand+1)+"type"));
        const auto dynPrefix=key(selectedSlot,"b")+juce::String(selectedBand+1)+"dyn";
        const bool dynOn=processor.value(dynPrefix+"on")>.5f;
        eqDynamicEnable.setEnabled(filterShape<=2);
        eqDynamicEnable.setButtonText(filterShape>2?"DYN N/A":dynOn?"DYNAMIC ON":"DYNAMIC OFF");
        eqDynamicEnable.setColour(juce::TextButton::buttonColourId,dynOn&&filterShape<=2?cyan:panel);
        eqDynamicEnable.setColour(juce::TextButton::textColourOffId,dynOn&&filterShape<=2?bg:uiText);
    }
    eqFilterType.setVisible(type==1);
    eqFilterLabel.setVisible(type==1);
    if(type==1){
        const auto target=selectedBand==0?key(selectedSlot,"eqtype"):
            key(selectedSlot,"b")+juce::String(selectedBand+1)+"type";
        eqFilterAttach=std::make_unique<ComboAttach>(processor.apvts,target,eqFilterType);
    }
    if(type==1){
        auto enabled=selectedBand==0?processor.value(key(selectedSlot,"eqon"))>.5f:processor.value(key(selectedSlot,"b")+juce::String(selectedBand+1)+"on")>.5f;
        eqBandEnable.setButtonText(enabled?"BAND ON":"BAND OFF");
        eqBandEnable.setEnabled(true);
    }
    graphTitle.setText(type==10?"PRECISION LIMITER  /  LOOKAHEAD 5 ms  /  LIVE GAIN REDUCTION":
        type==6?"ANALOG COLOR  /  TUBE - TAPE - TRANSFORMER":
        type==5?"DE-ESSER  /  LIVE SIBILANCE DETECTION":
        type==4?"FET 76  /  LIVE COMPRESSION TRANSFER":
        type==13?"OPTO LEVELER  /  LIVE COMPRESSION TRANSFER":
        "PRECISION EQ  /  12 BANDS  /  DYNAMIC CONTROL",juce::dontSendNotification);
    detailTitle.setText("MODULE "+juce::String(selectedSlot+1)+"  /  "+MixPilotProcessor::typeNames()[(int)param(processor,selectedSlot,"type")]+(type==1?"  /  BAND "+juce::String(selectedBand+1):""),juce::dontSendNotification);
}
void MixPilotEditor::refreshRack(){
    const auto names=MixPilotProcessor::typeNames();
    for(int i=0;i<MixPilotProcessor::slots;++i){
        const int type=(int)param(processor,i,"type");
        const bool enabled=param(processor,i,"enable")>.5f;
        auto& b=slots[i];
        b.setSelected(i==selectedSlot);
        b.setButtonText(juce::String(i+1)+"  "+names[type]+(enabled?"":"  [OFF]"));
        b.setColour(juce::TextButton::buttonColourId,i==selectedSlot?cyan:type==0?surface:panel);
        b.setColour(juce::TextButton::textColourOffId,i==selectedSlot?bg:enabled?uiText:muted);
    }
    bypass.setButtonText(param(processor,selectedSlot,"enable")>.5f?"BYPASS OFF":"BYPASS ON");
}
void MixPilotEditor::paint(juce::Graphics& g){
    g.fillAll(juce::Colour(0xff090c10));
    // Brushed graphite top bar and subtly warm panel boundaries.
    juce::ColourGradient top(juce::Colour(0xff24282d),0,0,juce::Colour(0xff0c1218),0,98,false);
    g.setGradientFill(top);g.fillRect(0,0,getWidth(),92);
    auto panelBox=[&](float x,float y,float w,float h,juce::Colour c){
        g.setColour(c);g.fillRoundedRectangle(x,y,w,h,12);
        g.setColour(juce::Colour(0xff48505a));g.drawRoundedRectangle(x,y,w,h,12,1);
    };
    panelBox(17,106,238,280,juce::Colour(0xff172029));
    panelBox(269,106,1251,280,juce::Colour(0xff141920));
    panelBox(17,401,1114,475,juce::Colour(0xff131c26));
    panelBox(1144,401,376,475,juce::Colour(0xff211e1c));
    panelBox(17,889,1503,44,juce::Colour(0xff191d23));
    g.setColour(juce::Colour(0xffbc9669));g.fillRoundedRectangle(1146,402,372,3,1);
    g.setColour(cyan);g.fillRoundedRectangle(19,402,1110,3,1);
    // Large module-stage labels, deliberately styled like hardware legends.
    g.setColour(juce::Colour(0xffb4a08b));g.setFont(13.0f);
    g.drawText("MIX ASSISTANT",31,118,200,21,juce::Justification::left);
    g.setColour(juce::Colour(0xff8aa0ab));g.setFont(12.0f);
    g.drawText("LOCAL ENGINE  /  LM STUDIO :1235",1210,40,290,22,juce::Justification::centredRight);
    g.setColour(juce::Colour(0xffd8c4a1));g.drawText("ENGINEER'S DESK",1162,416,330,18,juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff687989));g.drawText("INPUT     /     DYNAMICS     /     OUTPUT",1161,805,330,20,juce::Justification::centred);
    // Meter wells are painted from real DSP readings in timerCallback via repaint.
    const float levels[]={processor.inputPeak(),processor.outputPeak()};
    for(int i=0;i<2;++i){
        const auto x=1188.0f+i*145;
        g.setColour(juce::Colour(0xff080b0f));g.fillRoundedRectangle(x,690,89,102,4);
        const float dbv=dB(levels[i]);
        const float frac=juce::jlimit(0.0f,1.0f,(dbv+60.0f)/60.0f);
        const auto h=juce::jmax(0.0f,88.0f*frac);
        g.setColour(dbv>-3?juce::Colour(0xffe9a86d):juce::Colour(0xff65c997));
        g.fillRoundedRectangle(x+12,780-h,65,h,3);
        g.setColour(uiText);g.setFont(13.0f);
        g.drawText(i==0?"INPUT":"OUTPUT",x,664,92,24,juce::Justification::centred);
        g.drawText(juce::String(dbv,1)+" dBFS",x,787,92,23,juce::Justification::centred);
    }
}
void MixPilotEditor::resized(){
    title.setBounds(30,18,420,47);tagline.setBounds(31,67,600,20);
    modelLabel.setBounds(1158,66,80,22);
    modelIdEditor.setBounds(1240,63,265,29);
    rackTitle.setBounds(287,117,660,25);
    rackViewport.setBounds(280,152,1227,228);
    promptEditor.setBounds(30,148,211,111);
    scan.setBounds(30,268,211,31);generate.setBounds(30,305,211,35);
    sourceButton.setBounds(30,348,100,27);referenceButton.setBounds(134,348,107,27);
    sourceLabel.setBounds(1166,444,136,21);source.setBounds(1166,467,153,32);
    micLabel.setBounds(1331,444,150,21);mic.setBounds(1327,467,166,32);
    status.setBounds(1158,509,345,48);
    sourceStatus.setBounds(1158,558,345,20);referenceStatus.setBounds(1158,580,345,20);
    graphTitle.setBounds(37,411,660,35);
    graph.setBounds(31,450,1088,280);
    // Twelve selectable EQ bands share one graph and one precise control row.
    for(int i=0;i<MixPilotProcessor::eqBands;++i)
        bandSelectors[i].setBounds(44+i*44,734,39,30);
    eqBandEnable.setBounds(570,734,114,30);
    eqFilterLabel.setBounds(694,734,100,30);
    eqFilterType.setBounds(797,734,165,30);
    eqDynamicEnable.setBounds(973,734,132,30);
    detailTitle.setBounds(38,766,600,27);
    for(int i=0;i<7;++i){
        const int col=i;
        paramLabels[i].setBounds(43+col*151,794,143,16);
        controls[i].setBounds(41+col*151,810,143,64);
    }
    slotLabel.setBounds(1158,608,350,19);addModule.setBounds(1158,631,345,32);
    bypass.setBounds(1156,824,106,30);up.setBounds(1272,824,110,30);down.setBounds(1388,824,112,30);
    remove.setBounds(1156,858,345,25);
    meters.setBounds(28,896,354,30);diagnostics.setBounds(390,896,392,30);
    masterLabel.setBounds(787,897,70,20);master.setBounds(838,895,115,28);
    wetLabel.setBounds(964,897,45,20);wet.setBounds(996,895,130,28);
    intensityLabel.setBounds(1139,897,93,20);intensity.setBounds(1225,895,155,28);
}
void MixPilotEditor::timerCallback(){
    meters.setText("IN "+juce::String(dB(processor.inputPeak()),1)+" dB    OUT "+juce::String(dB(processor.outputPeak()),1)+" dB    GR "+juce::String(processor.reductionDb(),1)+" dB",juce::dontSendNotification);
    if(responseReady.exchange(false)){
        juce::String reply;
        {juce::ScopedLock l(responseMutex);reply=pendingResponse;}
        applyResponse(reply);requestActive.store(false);
    }
    if(waitingForAnalysis&&processor.analysisComplete()){
        waitingForAnalysis=false;
        // Copy the unprocessed track audio only AFTER capture has completed.
        // All FFT and profile processing happens here on the message thread,
        // never inside processBlock. Reuses exactly the WAV-analysis math.
        std::vector<float> liveSamples;
        SpectralProfile liveProfile;
        if(processor.copyLiveCapture(liveSamples)){
            mixpilot::SpectralProfiler analyzer(processor.sampleRateHz());
            analyzer.push(liveSamples.begin(),liveSamples.end());
            const auto result=analyzer.result();
            liveProfile.valid=result.valid;
            liveProfile.relativeDb=result.relativeDb;
        }
        sourceSpectrum=liveProfile;
        processor.setSpectralProfile(false,liveProfile);
        SpectralProfile processedProfile;
        std::vector<float> processedSamples;
        if(processor.copyProcessedCapture(processedSamples)){
            mixpilot::SpectralProfiler after(processor.sampleRateHz());
            after.push(processedSamples.begin(),processedSamples.end());
            const auto result=after.result();
            processedProfile.valid=result.valid;
            processedProfile.relativeDb=result.relativeDb;
        }
        sourceStatus.setText(liveProfile.valid?"SOURCE  /  live spectrum (18 regions)":
            "SOURCE  /  live spectrum insufficient audio",juce::dontSendNotification);
        auto f=processor.features();
        status.setText("Measured RMS "+juce::String(f.rmsDb,1)+" dBFS, crest "+
            juce::String(f.crestDb,1)+" dB. "+
            (liveProfile.valid?"18-region spectrum ready for reference matching.":
              "Insufficient vocal audio for detailed EQ matching.")+
            (liveProfile.valid && processedProfile.valid && referenceSpectrum.valid ?
                " Tonal distance: dry " + juce::String(mixpilot::planning::spectralDistanceDb(
                    liveProfile.relativeDb,referenceSpectrum.relativeDb),1)+
                " dB / processed " + juce::String(mixpilot::planning::spectralDistanceDb(
                    processedProfile.relativeDb,referenceSpectrum.relativeDb),1)+
                " dB (lower is closer spectrally; not a sound-quality score)." : ""),
            juce::dontSendNotification);
        auto res=processor.resonance();
        diagnostics.setText(res.hz>0?"POSSIBLE peak: "+juce::String((int)res.hz)+" Hz, prominence "+juce::String(res.prominenceDb,1)+" dB (verify by ear)":"No narrow-band candidate identified",juce::dontSendNotification);
    }
    std::array<float,MixPilotProcessor::spectrumSize> samples{};
    processor.spectrum(samples);
    fftData.fill(0);
    constexpr int n=MixPilotProcessor::spectrumSize;
    for(int i=0;i<n;++i)fftData[(size_t)i]=samples[(size_t)i]*(.5f-.5f*std::cos(2*juce::MathConstants<double>::pi*i/(n-1)));
    fft.performFrequencyOnlyForwardTransform(fftData.data(),true);
    for(int i=0;i<128;++i){
        const float f=20*std::pow(1000.0f,i/127.0f);
        const int bin=juce::jlimit(1,n/2-1,(int)std::round(f*n/processor.sampleRateHz()));
        const float mag=juce::jmax(fftData[(size_t)bin],fftData[(size_t)juce::jmin(n/2-1,bin+1)]);
        const float level=dB(mag/n*4);
        graph.spectrum[(size_t)i]=graph.spectrum[(size_t)i]*.7f+level*.3f;
    }
    graph.repaint();
    const int meterModuleType=(int)param(processor,selectedSlot,"type");
    if(meterModuleType==4 || meterModuleType==13 || meterModuleType==10)
        slots[selectedSlot].repaint();
}
bool MixPilotEditor::analyzeFile(const juce::File& file,bool reference){
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if(!reader||reader->sampleRate<8000||reader->numChannels<1)return false;
    const auto length=juce::jmin(reader->lengthInSamples,(juce::int64)(reader->sampleRate*180.0));
    if(length<1)return false;
    juce::AudioBuffer<float> audio(juce::jmin(2,(int)reader->numChannels),8192);
    const float lowCoefficient=std::exp(-2.0*juce::MathConstants<double>::pi*210.0/reader->sampleRate);
    const float highCoefficient=std::exp(-2.0*juce::MathConstants<double>::pi*4700.0/reader->sampleRate);
    float lp[2]{},hp[2]{},peak=0;
    double rms=0,low=0,mid=0,high=0,total=0;
    double leftEnergy=0,rightEnergy=0,crossEnergy=0;
    double quietWindowEnergy=0,minimumWindowEnergy=1.0e30;
    int quietWindowFrames=0;
    const bool stereo=audio.getNumChannels()==2;
    // Shared with live capture: the same gating, Hann FFT, 18 log bands,
    // normalization and validity criteria. Runs off the audio thread.
    mixpilot::SpectralProfiler spectralAnalyzer(reader->sampleRate);
    for(juce::int64 pos=0;pos<length;pos+=audio.getNumSamples()){
        const int count=(int)juce::jmin((juce::int64)audio.getNumSamples(),length-pos);
        if(!reader->read(&audio,0,count,pos,true,true))return false;
        for(int c=0;c<audio.getNumChannels();++c){
            const auto* samples=audio.getReadPointer(c);
            for(int i=0;i<count;++i){
                const float x=samples[i];
                lp[c]=lowCoefficient*lp[c]+(1-lowCoefficient)*x;
                hp[c]=highCoefficient*hp[c]+(1-highCoefficient)*x;
                const float lo=lp[c],hi=x-hp[c],md=x-lo-hi;
                rms+=x*x;low+=lo*lo;mid+=md*md;high+=hi*hi;
                peak=juce::jmax(peak,std::abs(x));++total;
            }
        }
        // Stereo measurements and quietest-window estimate use aligned frames.
        for(int i=0;i<count;++i){
            const double left=audio.getReadPointer(0)[i];
            const double right=stereo?audio.getReadPointer(1)[i]:0.0;
            if(stereo){leftEnergy+=left*left;rightEnergy+=right*right;crossEnergy+=left*right;}
            quietWindowEnergy+=(left*left+(stereo?right*right:0.0))/(stereo?2.0:1.0);
            spectralAnalyzer.push((float)(stereo?0.5*(left+right):left));
            if(++quietWindowFrames==1024){
                minimumWindowEnergy=juce::jmin(minimumWindowEnergy,quietWindowEnergy/1024.0);
                quietWindowEnergy=0;quietWindowFrames=0;
            }
        }
    }
    if(total<1)return false;
    ToneFeatures f;
    f.rmsDb=dB((float)std::sqrt(rms/total));f.lowDb=dB((float)std::sqrt(low/total));
    f.midDb=dB((float)std::sqrt(mid/total));f.highDb=dB((float)std::sqrt(high/total));
    f.peakDb=dB(peak);
    f.crestDb=juce::jlimit(0.0f,90.0f,f.peakDb-f.rmsDb);
    f.stereoValid=stereo&&leftEnergy>1.0e-12&&rightEnergy>1.0e-12;
    if(f.stereoValid){
        f.stereoCorrelation=(float)juce::jlimit(-1.0,1.0,crossEnergy/std::sqrt(leftEnergy*rightEnergy));
        f.stereoBalanceDb=(float)juce::jlimit(-60.0,60.0,10.0*std::log10(leftEnergy/rightEnergy));
    }
    f.quietestWindowDb=minimumWindowEnergy<1.0e29?
        dB((float)std::sqrt(minimumWindowEnergy)):-90.0f;
    f.valid=true;
    SpectralProfile spectral;
    const auto profile=spectralAnalyzer.result();
    spectral.valid=profile.valid;
    spectral.relativeDb=profile.relativeDb;
    if(reference){
        processor.setReference(f);
        processor.setSpectralProfile(true,spectral);
        referenceSpectrum=spectral;
        referenceStatus.setText("REFERENCE  /  "+file.getFileName(),juce::dontSendNotification);
    }else{
        processor.setFileFeatures(f);
        processor.setSpectralProfile(false,spectral);
        sourceSpectrum=spectral;
        sourceStatus.setText("SOURCE  /  "+file.getFileName(),juce::dontSendNotification);
    }
    return true;
}
void MixPilotEditor::deliverResponse(juce::String value){juce::ScopedLock l(responseMutex);pendingResponse=std::move(value);responseReady.store(true);}
juce::String MixPilotEditor::callLocalModel(const juce::String& instruction,const ToneFeatures& audio,const ToneFeatures& ref,int mic,int mode,ResonanceHint suspect,SpectralProfile sourceSpectrum,SpectralProfile referenceSpectrum,const juce::String& requestedModel){
    const juce::String micText=mic==1?"Neumann TLM 103":mic==2?"generic dynamic mic":"unknown";
    const juce::String source=juce::StringArray{"Universal","Vocals","Instrument","Drums","Stereo Mix"}[juce::jlimit(0,4,mode)];
    juce::String measurement="Actual measured source (not direct AI hearing): "+source+
        ", mic="+micText+", RMS="+juce::String(audio.rmsDb,2)+" dBFS, peak="+juce::String(audio.peakDb,2)+
        " dBFS, low (<210Hz)="+juce::String(audio.lowDb,2)+", mid (210-4700Hz)="+juce::String(audio.midDb,2)+
        ", high (>4700Hz)="+juce::String(audio.highDb,2)+" dBFS.";
    measurement+=" Crest factor="+juce::String(audio.crestDb,2)+
        " dB; quietest 1024-frame RMS="+juce::String(audio.quietestWindowDb,2)+
        " dBFS (may include silence; NOT a calibrated noise floor).";
    if(audio.stereoValid)measurement+=" Stereo correlation="+
        juce::String(audio.stereoCorrelation,3)+"; L/R energy difference="+
        juce::String(audio.stereoBalanceDb,2)+" dB.";
    else measurement+=" Stereo measurements unavailable (mono or silent channel).";
    if(suspect.hz>0)measurement+=" Possible resonance at "+juce::String(suspect.hz,1)+"Hz, prominence "+juce::String(suspect.prominenceDb,1)+"dB. Low confidence.";
    if(ref.valid){
        measurement+=" Reference RMS "+juce::String(ref.rmsDb,2)+", low "+
            juce::String(ref.lowDb,2)+", mid "+juce::String(ref.midDb,2)+
            ", high "+juce::String(ref.highDb,2)+" dBFS; reference crest="+
            juce::String(ref.crestDb,2)+" dB.";
        measurement+=" Measured differences (source minus reference): RMS="+
            juce::String(audio.rmsDb-ref.rmsDb,2)+" dB, low="+
            juce::String(audio.lowDb-ref.lowDb,2)+" dB, mid="+
            juce::String(audio.midDb-ref.midDb,2)+" dB, high="+
            juce::String(audio.highDb-ref.highDb,2)+" dB, crest="+
            juce::String(audio.crestDb-ref.crestDb,2)+" dB.";
        if(ref.stereoValid&&audio.stereoValid)
            measurement+=" Correlation difference="+
                juce::String(audio.stereoCorrelation-ref.stereoCorrelation,3)+".";
    }
    if(sourceSpectrum.valid){
        measurement+=" Measured source spectral regions (level-normalized relative dB; not diagnosis): ";
        for(int b=0;b<SpectralProfile::bands;++b){
            const float hz=30.0f*std::pow(16000.0f/30.0f,((float)b+0.5f)/(float)SpectralProfile::bands);
            measurement+=juce::String(hz,0)+"Hz:"+juce::String(sourceSpectrum.relativeDb[(size_t)b],1)+"dB ";
        }
        if(referenceSpectrum.valid){
            measurement+=" Source minus reference spectrum (LEVEL-NORMALIZED dB; positive means relatively more energy in source): ";
            for(int b=0;b<SpectralProfile::bands;++b){
                const float hz=30.0f*std::pow(16000.0f/30.0f,((float)b+0.5f)/(float)SpectralProfile::bands);
                const float difference=sourceSpectrum.relativeDb[(size_t)b]-referenceSpectrum.relativeDb[(size_t)b];
                measurement+=juce::String(hz,0)+"Hz:"+juce::String(difference,1)+"dB ";
            }
            measurement+=" These regional differences are clues only, not a prescription for exact inverse EQ. ";
        }
    }
    measurement+=" User target: "+instruction;
    const juce::String system=R"(You are MixPilot's local mixing planner. The user gives a musical goal in normal language. Artist/song names give style context; only supplied audio measurements are evidence. Make deliberate, measured mixing choices, using source/reference spectral comparisons when available. For vocals, propose a useful chain of 5-8 modules when appropriate rather than stopping at high-pass, EQ and compression. Use frequency corrections grounded in the supplied data. Never claim to have heard audio or to have measured something not provided.
Return ONLY a small valid JSON object: {"modules":[{"type":1,"bands":[{"index":1,"freq":300,"gain":-2,"q":1.2,"filter":0}]},{"type":4,"amount":35,"ratio":3}],"explanation":"brief measured rationale"}. No code fences or reasoning. At most TEN module objects; for a full polished vocal target generally use one Precision EQ containing optional HPF/LPF bands, dynamics, de-esser, tasteful saturation and modest space when supported by the user goal. Omit effects the user explicitly rejects. All objects MUST include numeric type. If type 1 EQ is chosen, MUST include 3-7 specific bands when measured tonal correction is justified; otherwise omit EQ. No generic EQ without bands. Do not use a fixed 80 Hz high-pass: choose whether and where to filter based on the vocal and measured low-frequency comparison. An EQ band with filter 4 or 5 has gain 0 and Q around 0.707. If the supplied reference suggests a spatial production, consider subtle reverb and delay. Stereo side energy is only a clue, not proof of a specific effect. EQ bands: index 1..12, freq 20..20000, gain -18..18 (prefer corrections within +/-4 when justified), q 0.3..12, filter 0 bell/1 low shelf/2 high shelf/3 notch/4 HPF/5 LPF, optional dynamic boolean, threshold -60..0, range -12..12, attack 1..100, release 20..600. Source-reference regional spectral differences are uncertain clues, not precise resonances; avoid drastic inverse-EQ.
Module types: 1 EQ (ALWAYS put HPF/LPF as bands in this module, never separate types 2 or 3), 2/3 legacy filter modules (do not create), 4 FET compressor, 5 de-esser, 6 saturation, 7 gain, 8 width, 9 delay, 10 limiter, 11 reverb, 12 phaser, 13 opto compressor, 14 chorus, 15 flanger. Duplicate types allowed. Use up to 10 slots and no more than one limiter. Common optional fields: freq 20..20000, gain -18..18, q 0.3..12, amount 0..100, ratio 1..12, attack 0.1..200, release 10..1000, mix 0..100, slope 0/1. De-esser: deessThreshold -60..0, deessRange 0..18, deessMode 0 split/1 wide, freq 4000..10000. Saturation: use Drive amount 55..80 and mix 75..100 for aggressive/gritty/rock vocals when requested; otherwise use modest Drive 20..40, mix 50..80. satMode 0 tube/1 tape/2 transformer, satQuality 0/1/2, satTone -100..100, satBias -100..100, satOutput -18..12. Limiter (final output, fixed 5ms delay): limInput -12..18, limCeiling -12..-0.1, limRelease 40..1000, limTruePeak 0/1. Reverb: spaceMode 0 plate/1 room/2 hall, spaceDecay 0.3..8, spacePredelay 0..150, spaceDamp 0..100, spaceLowCut 20..1000, spaceHighCut 2000..18000. Delay: delayMode 0 stereo/1 pingpong/2 tape, delayMs 30..1000, delaySync 0/1, delayDivision 0..3, delayFeedback 0..85, delayTone 0..100, delayDuck 0..100. Modulation: modRate 0.05..8, modDepth 0..100, modFeedback -85..85, modStereo 0..180. For reverb/delay/chorus/flanger use subtle mix near 15..25 initially. Use sensible gain staging and safe conservative defaults. The explanation must be short. Output JSON immediately, not your thinking.)";
    // Resolve model on this background inference job. No network calls on the audio thread.
    // /v1/models includes downloaded models on LM Studio servers with JIT enabled.
    juce::String modelId=requestedModel.trim();
    if(modelId.isEmpty()||modelId.equalsIgnoreCase("AUTO")){
#if JUCE_WINDOWS
        struct ModelHandle { HINTERNET value=nullptr; ~ModelHandle(){if(value)WinHttpCloseHandle(value);} };
        ModelHandle session,connection,request;
        session.value=WinHttpOpen(L"MixPilot/8.9.2",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
        if(!session.value)return "ERROR: Could not initialize LM Studio model discovery.";
        WinHttpSetTimeouts(session.value,4000,4000,4000,7000);
        connection.value=WinHttpConnect(session.value,L"127.0.0.1",1235,0);
        if(!connection.value)return "ERROR: Cannot connect to LM Studio at localhost:1235.";
        request.value=WinHttpOpenRequest(connection.value,L"GET",L"/v1/models",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
        if(!request.value || !WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)
           || !WinHttpReceiveResponse(request.value,nullptr))
            return "ERROR: Cannot read LM Studio /v1/models. Check that the API server is running on port 1235.";
        DWORD httpCode=0,httpSize=sizeof(httpCode);
        WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,&httpCode,&httpSize,WINHTTP_NO_HEADER_INDEX);
        if(httpCode!=200)return "ERROR: LM Studio model discovery HTTP "+juce::String((int)httpCode);
        std::string bytes;
        while(bytes.size()<1024*1024){
            char chunk[4096]; DWORD received=0;
            if(!WinHttpReadData(request.value,chunk,sizeof(chunk),&received))
                return "ERROR: Could not read the LM Studio model list.";
            if(received==0)break;
            bytes.append(chunk,(size_t)received);
        }
        const auto result=juce::JSON::parse(juce::String::fromUTF8(bytes.data(),(int)bytes.size()));
        if(auto* document=result.getDynamicObject()){
            const auto entryList=document->getProperty("data");
            if(auto* entries=entryList.getArray()){
                for(const auto& entry:*entries){
                    if(auto* details=entry.getDynamicObject()){
                        const auto id=details->getProperty("id").toString().trim();
                        if(id.isEmpty())continue;
                        // Prefer the user's chosen Qwen2.5, where installed. Otherwise use the first advertised model.
                        if(modelId.isEmpty()||modelId.equalsIgnoreCase("AUTO"))modelId=id;
                        if(id.containsIgnoreCase("qwen2.5-14b-instruct")){modelId=id;break;}
                    }
                }
            }
        }
#else
        return "ERROR: LM Studio discovery currently requires Windows.";
#endif
        if(modelId.isEmpty()||modelId.equalsIgnoreCase("AUTO"))
            return "ERROR: No models found through LM Studio /v1/models. Load Qwen2.5 in LM Studio.";
    }
    juce::DynamicObject::Ptr payload=new juce::DynamicObject();
    payload->setProperty("model",modelId);
    payload->setProperty("max_tokens",8192);
    payload->setProperty("temperature",0.2);
    payload->setProperty("stream",false);
    juce::Array<juce::var> messages;
    for(const auto& pair:{std::pair<juce::String,juce::String>{"system",system},{"user",measurement}}){
        juce::DynamicObject::Ptr o=new juce::DynamicObject();
        o->setProperty("role",pair.first);o->setProperty("content",pair.second);
        messages.add(juce::var(o.get()));
    }
    payload->setProperty("messages",juce::var(messages));
    const std::string body=juce::JSON::toString(juce::var(payload.get())).toStdString();
    juce::String response;
    int code=0;
#if JUCE_WINDOWS
    struct Handle{HINTERNET p=nullptr;~Handle(){if(p)WinHttpCloseHandle(p);}};
    Handle session,connection,request;
    session.p=WinHttpOpen(L"MixPilotAI5/5.0",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session.p)return "ERROR: Cannot create Windows HTTP session.";
    WinHttpSetTimeouts(session.p,5000,5000,10000,180000);
    connection.p=WinHttpConnect(session.p,L"127.0.0.1",1235,0);
    if(!connection.p)return "ERROR: Cannot reach LM Studio at 127.0.0.1:1235.";
    request.p=WinHttpOpenRequest(connection.p,L"POST",L"/v1/chat/completions",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
    if(!request.p)return "ERROR: Could not create LM Studio request.";
    const wchar_t* headers=L"Content-Type: application/json\r\nAccept: application/json\r\n";
    const DWORD length=(DWORD)body.size();
    if(!WinHttpSendRequest(request.p,headers,(DWORD)-1,const_cast<char*>(body.data()),length,length,(DWORD_PTR)0))
        return "ERROR: WinHTTP send error "+juce::String((int)GetLastError());
    if(!WinHttpReceiveResponse(request.p,nullptr))return "ERROR: LM Studio response error "+juce::String((int)GetLastError());
    DWORD status=0,statusSize=sizeof(status);
    if(WinHttpQueryHeaders(request.p,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&statusSize,WINHTTP_NO_HEADER_INDEX))code=(int)status;
    std::string raw;
    while(raw.size()<4*1024*1024){
        char buffer[8192];DWORD count=0;
        if(!WinHttpReadData(request.p,buffer,sizeof(buffer),&count))return "ERROR: LM Studio read error "+juce::String((int)GetLastError());
        if(count==0)break;
        raw.append(buffer,(size_t)count);
    }
    response=juce::String::fromUTF8(raw.data(),(int)raw.size());
#else
    return "ERROR: This local inference bridge currently targets Windows.";
#endif
    if(code<200||code>=300)return "ERROR: LM Studio HTTP "+juce::String(code)+" "+response.substring(0,150);
    auto parsed=juce::JSON::parse(response);
    if(auto* root=parsed.getDynamicObject()){
        auto a=root->getProperty("choices");
        if(auto* choices=a.getArray())if(!choices->isEmpty()){
            auto* choice=choices->getReference(0).getDynamicObject();
            if(choice)if(auto* message=choice->getProperty("message").getDynamicObject()){
                const auto finalText=message->getProperty("content").toString().trim();
                if(finalText.isNotEmpty())return finalText;
                const auto reason=choice->getProperty("finish_reason").toString();
                const auto reasoning=message->getProperty("reasoning_content").toString();
                if(reason=="length")
                    return "ERROR: Qwen used its output token budget without a final answer. Disable Thinking in LM Studio and retry.";
                if(reasoning.isNotEmpty())
                    return "ERROR: LM Studio returned reasoning but no final answer. Disable Thinking in LM Studio and retry.";
                return "ERROR: LM Studio returned empty AI content (finish_reason="+reason+"). Check model and Thinking settings.";
            }
        }
    }
    return "ERROR: Invalid LM Studio response.";
}
void MixPilotEditor::requestMix(){
    const auto measured=processor.features();
    if(!measured.valid){status.setText("Analyze live audio or load source WAV first.",juce::dontSendNotification);return;}
    if(requestActive.exchange(true)){status.setText("AI request already running.",juce::dontSendNotification);return;}
    const auto ref=processor.reference();
    const auto micId=(int)processor.value("mic"),sourceId=(int)processor.value("mode");
    const auto resonance=processor.resonance();
    const auto target=promptEditor.getText().trim().isEmpty()
        ? juce::String("Make my vocal sound polished and similar to the loaded reference.")
        : promptEditor.getText();
    const auto srcSpectrum=sourceSpectrum,refSpectrum=referenceSpectrum;
    const auto chosenModel=modelIdEditor.getText().trim();
    status.setText("AI model "+(chosenModel.isEmpty()?juce::String("AUTO"):chosenModel)+" - planning the mix...",juce::dontSendNotification);
    class Job final : public juce::ThreadPoolJob{
    public:
        Job(std::function<juce::String()> f,std::function<void(juce::String)> cb):ThreadPoolJob("MixPilot inference"),work(std::move(f)),done(std::move(cb)){}
        JobStatus runJob() override {auto result=work();if(!shouldExit())done(std::move(result));return jobHasFinished;}
    private:
        std::function<juce::String()> work;
        std::function<void(juce::String)> done;
    };
    inferencePool.addJob(new Job([target,measured,ref,micId,sourceId,resonance,srcSpectrum,refSpectrum,chosenModel]{return callLocalModel(target,measured,ref,micId,sourceId,resonance,srcSpectrum,refSpectrum,chosenModel);},
                                 [this](juce::String s){deliverResponse(std::move(s));}),true);
}
void MixPilotEditor::applyResponse(const juce::String& result){
    if(result.startsWith("ERROR:")){status.setText(result,juce::dontSendNotification);return;}
    const int first=result.indexOfChar('{'),last=result.lastIndexOfChar('}');
    if(first<0||last<=first){status.setText("AI returned no JSON. Existing chain preserved.",juce::dontSendNotification);return;}
    const auto doc=juce::JSON::parse(result.substring(first,last+1));
    auto* o=doc.getDynamicObject();
    if(!o){status.setText("AI returned invalid JSON. Existing chain preserved.",juce::dontSendNotification);return;}
    auto v=o->getProperty("modules");
    auto* arr=v.getArray();
    if(!arr||arr->isEmpty()||arr->size()>MixPilotProcessor::slots){status.setText("AI module list invalid. Existing chain preserved.",juce::dontSendNotification);return;}
    using Band=mixpilot::planning::VocalBand;
    using Mod=mixpilot::planning::VocalModule;
    std::array<Mod,MixPilotProcessor::slots> proposed{};
    auto number=[](juce::var v,float lo,float hi,float& target){
        if(v.isVoid())return true;
        if(!(v.isInt()||v.isInt64()||v.isDouble()))return false;
        const float value=(float)(double)v;
        if(!std::isfinite(value))return false;
        target=juce::jlimit(lo,hi,value);return true;
    };
    for(int i=0;i<arr->size();++i){
        auto* module=arr->getReference(i).getDynamicObject();
        if(!module){status.setText("AI module malformed. Chain unchanged.",juce::dontSendNotification);return;}
        float type=-1;
        if(!number(module->getProperty("type"),1,15,type)||type<1||type>15||type!=std::floor(type)){status.setText("AI module type invalid. Chain unchanged.",juce::dontSendNotification);return;}
        auto& m=proposed[(size_t)i];m.type=(int)type;
        if(m.type==5)m.freq=6500;
        if(m.type==2)m.freq=mixpilot::planning::sourceReferenceHighPassHz(sourceSpectrum.relativeDb,referenceSpectrum.relativeDb,sourceSpectrum.valid&&referenceSpectrum.valid);
        if(m.type==3)m.freq=16000;
        if(m.type==9||m.type==11||m.type==14||m.type==15)m.mix=25;
        if(!number(module->getProperty("freq"),20,20000,m.freq)||
           !number(module->getProperty("gain"),-18,18,m.gain)||
           !number(module->getProperty("q"),.3f,12,m.q)||
           !number(module->getProperty("amount"),0,100,m.amount)||
           !number(module->getProperty("ratio"),1,12,m.ratio)||
           !number(module->getProperty("attack"),.1f,200,m.attack)||
           !number(module->getProperty("release"),10,1000,m.release)||
           !number(module->getProperty("mix"),0,100,m.mix)||
           !number(module->getProperty("slope"),0,1,m.slope)||
           !number(module->getProperty("deessThreshold"),-60,0,m.deessThreshold)||
           !number(module->getProperty("deessRange"),0,18,m.deessRange)||
           !number(module->getProperty("deessMode"),0,1,m.deessMode)||
           !number(module->getProperty("satMode"),0,2,m.satMode)||
           !number(module->getProperty("satQuality"),0,2,m.satQuality)||
           !number(module->getProperty("satTone"),-100,100,m.satTone)||
           !number(module->getProperty("satBias"),-100,100,m.satBias)||
           !number(module->getProperty("satOutput"),-18,12,m.satOutput)||
           !number(module->getProperty("limInput"),-12,18,m.limInput)||
           !number(module->getProperty("limCeiling"),-12,-0.1f,m.limCeiling)||
           !number(module->getProperty("limRelease"),40,1000,m.limRelease)||
           !number(module->getProperty("limTruePeak"),0,1,m.limTruePeak)||
           !number(module->getProperty("spaceMode"),0,2,m.spaceMode)||
           !number(module->getProperty("spaceDecay"),.3f,8,m.spaceDecay)||
           !number(module->getProperty("spacePredelay"),0,150,m.spacePredelay)||
           !number(module->getProperty("spaceDamp"),0,100,m.spaceDamp)||
           !number(module->getProperty("spaceLowCut"),20,1000,m.spaceLowCut)||
           !number(module->getProperty("spaceHighCut"),2000,18000,m.spaceHighCut)||
           !number(module->getProperty("delayMode"),0,2,m.delayMode)||
           !number(module->getProperty("delayMs"),30,1000,m.delayMs)||
           !number(module->getProperty("delaySync"),0,1,m.delaySync)||
           !number(module->getProperty("delayDivision"),0,3,m.delayDivision)||
           !number(module->getProperty("delayFeedback"),0,85,m.delayFeedback)||
           !number(module->getProperty("delayTone"),0,100,m.delayTone)||
           !number(module->getProperty("delayDuck"),0,100,m.delayDuck)||
           !number(module->getProperty("modRate"),.05f,8,m.modRate)||
           !number(module->getProperty("modDepth"),0,100,m.modDepth)||
           !number(module->getProperty("modFeedback"),-85,85,m.modFeedback)||
           !number(module->getProperty("modStereo"),0,180,m.modStereo)){
            status.setText("AI parameter invalid. Chain unchanged.",juce::dontSendNotification);return;
        }
        const auto bandsValue=module->getProperty("bands");
        if(!bandsValue.isVoid()){
            auto* bands=bandsValue.getArray();
            if(m.type!=1||bands==nullptr||bands->size()>MixPilotProcessor::eqBands){
                status.setText("AI EQ bands invalid. Chain unchanged.",juce::dontSendNotification);return;
            }
            std::array<bool,MixPilotProcessor::eqBands> seen{};
            for(const auto& candidate:*bands){
                auto* b=candidate.getDynamicObject();
                if(!b){status.setText("AI band malformed. Chain unchanged.",juce::dontSendNotification);return;}
                Band entry;
                float idx=0,shape=0;
                if(!number(b->getProperty("index"),1,12,idx)||idx!=std::floor(idx)||
                   !number(b->getProperty("filter"),0,5,shape)||shape!=std::floor(shape)||
                   !number(b->getProperty("freq"),20,20000,entry.freq)||
                   !number(b->getProperty("gain"),-18,18,entry.gain)||
                   !number(b->getProperty("q"),.3f,12,entry.q)||
                   !number(b->getProperty("threshold"),-60,0,entry.threshold)||
                   !number(b->getProperty("range"),-12,12,entry.range)||
                   !number(b->getProperty("attack"),1,100,entry.attack)||
                   !number(b->getProperty("release"),20,600,entry.release)){
                    status.setText("AI band value invalid. Chain unchanged.",juce::dontSendNotification);return;
                }
                entry.index=(int)idx;entry.filter=(int)shape;
                if(entry.index<1||entry.index>12||seen[(size_t)(entry.index-1)]){
                    status.setText("AI band index invalid. Chain unchanged.",juce::dontSendNotification);return;
                }
                seen[(size_t)(entry.index-1)]=true;
                const auto dynamic=b->getProperty("dynamic");
                if(!dynamic.isVoid()&&!dynamic.isBool()){
                    status.setText("AI dynamic flag invalid. Chain unchanged.",juce::dontSendNotification);return;
                }
                entry.dynamic=dynamic.isBool() && (bool)dynamic && entry.filter<=2;
                m.bands.push_back(entry);
            }
        }
    }
    int plannedCount=arr->size();
    const auto request=promptEditor.getText().toLowerCase();
    const bool dry=request.contains("dry vocal")||request.contains("no reverb")||
        request.contains("no delay")||request.contains("without effects")||request.contains("completely dry");
    const bool minimal=request.contains("only eq")||request.contains("only compression")||
        request.contains("minimal processing")||request.contains("no other processing");
    const bool referenceMatch=(referenceSpectrum.valid || processor.reference().valid) &&
        (request.contains("reference")||request.contains("similar to")||request.contains("match")||
         request.contains("sound like")||request.contains("production style"));
    int combinedFilters=0;
    const float referenceCutoff=mixpilot::planning::sourceReferenceHighPassHz(
        sourceSpectrum.relativeDb,referenceSpectrum.relativeDb,
        sourceSpectrum.valid&&referenceSpectrum.valid);
    plannedCount=mixpilot::planning::consolidateCutModules(proposed,plannedCount,
                                                           referenceCutoff,combinedFilters);
    mixpilot::planning::Options options;
    options.vocal=(int)processor.value("mode")==1;
    options.dry=dry;
    options.minimal=minimal;
    options.referenceMatch=referenceMatch;
    const auto refFeatures=processor.reference();
    options.hasReferenceSpatialCue=mixpilot::planning::referenceShowsStereoSpace(
        refFeatures.stereoValid,refFeatures.stereoCorrelation);
    options.requestDelay=request.contains("delay")||request.contains("echo")||request.contains("slapback");
    options.requestReverb=request.contains("reverb")||request.contains("space")||request.contains("depth");
    options.requestSaturation=request.contains("saturat")||request.contains("grit")||
        request.contains("rock")||request.contains("aggress")||request.contains("bennington");
    int autoAdded=0;
    plannedCount=mixpilot::planning::completeVocalPlan(proposed,plannedCount,options,autoAdded);
    const int measuredLowCuts=mixpilot::planning::supplementMeasuredLowCut(
        proposed,plannedCount,sourceSpectrum.relativeDb,referenceSpectrum.relativeDb,
        sourceSpectrum.valid&&referenceSpectrum.valid);
    // The planner may add a new EQ. Keep the original reference-based EQ suggestions below.
    // Supplement AI bands with broad measured source-reference differences.
    // Previously, three weak AI bands suppressed the entire measured fallback.
    // Frequency-region differences are not narrow resonances; don't invent any.
    int assistedBands=0;
    for(int i=0;i<plannedCount;++i){
        auto& m=proposed[(size_t)i];
        if(m.type!=1)continue;
        if(sourceSpectrum.valid && referenceSpectrum.valid && m.bands.size()<8){
            const auto suggestions=mixpilot::referenceEqSuggestions(
                sourceSpectrum.relativeDb,referenceSpectrum.relativeDb);
            for(int k=0;k<suggestions.count && m.bands.size()<8;++k){
                const auto& candidate=suggestions.items[(size_t)k];
                bool overlaps=false;
                for(const auto& band:m.bands){
                    if(std::abs(std::log2(juce::jmax(20.0f,band.freq)/candidate.hz))<0.42f){
                        overlaps=true;break;
                    }
                }
                if(overlaps)continue; // Keep Qwen's existing band near that region.
                bool usedIndex[MixPilotProcessor::eqBands]{};
                for(const auto& band:m.bands)
                    if(band.index>=1 && band.index<=MixPilotProcessor::eqBands)
                        usedIndex[band.index-1]=true;
                int freeIndex=0;
                for(int id=0;id<MixPilotProcessor::eqBands;++id)
                    if(!usedIndex[id]){freeIndex=id+1;break;}
                if(freeIndex==0)break;
                Band band;
                band.index=freeIndex;
                band.freq=candidate.hz;
                band.gain=candidate.gainDb;
                band.q=candidate.q;
                m.bands.push_back(band);
                ++assistedBands;
            }
        }
        // If only live measurements exist, honor one *measured* resonance hint
        // instead of inventing several cuts or arbitrary presence boosts.
        const auto resonance=processor.resonance();
        if(m.bands.empty() && resonance.hz>=130 && resonance.hz<=11000 && resonance.prominenceDb>=4.0f){
            Band b;b.index=1;b.freq=resonance.hz;
            b.gain=-juce::jlimit(1.0f,3.5f,0.35f*resonance.prominenceDb);
            b.q=2.5f;m.bands.push_back(b);++assistedBands;
        }
    }
    const float strength=processor.value("intensity")*.01f;
    const auto vocalGoal=promptEditor.getText().toLowerCase();
    const bool wantsSaturatedVocal=!vocalGoal.contains("no saturation") &&
        (vocalGoal.contains("grit") || vocalGoal.contains("aggress") ||
         vocalGoal.contains("distort") || vocalGoal.contains("rock") ||
         vocalGoal.contains("bennington") || vocalGoal.contains("saturat"));
    for(int i=0;i<MixPilotProcessor::slots;++i){
        const auto& m=proposed[(size_t)i];
        processor.setValue(key(i,"type"),(float)m.type);
        processor.setValue(key(i,"enable"),1);
        if(m.type==0)continue;
        processor.setValue(key(i,"freq"),m.freq);
        processor.setValue(key(i,"gain"),m.gain*strength);
        processor.setValue(key(i,"q"),m.q);
        // Only for an explicitly aggressive/rock vocal goal, prevent a very
        // timid AI saturation setting from making the requested grit inaudible.
        // Manual knob changes remain completely unrestricted afterward.
        const float effectiveAmount=(m.type==6 && wantsSaturatedVocal) ?
            juce::jmax(m.amount,65.0f)*strength : m.amount*strength;
        processor.setValue(key(i,"amount"),juce::jlimit(0.0f,100.0f,effectiveAmount));
        processor.setValue(key(i,"ratio"),m.ratio);
        processor.setValue(key(i,"attack"),m.attack);
        processor.setValue(key(i,"release"),m.release);
        processor.setValue(key(i,"mix"),
            m.type==6 && wantsSaturatedVocal ? juce::jmax(75.0f,m.mix) : m.mix);
        processor.setValue(key(i,"slope"),m.slope);
        if(m.type==11){
            for(const auto* k:{"spaceMode","spaceDecay","spacePredelay","spaceDamp","spaceLowCut","spaceHighCut"}){
                float val=0;
                if(juce::String(k)=="spaceMode")val=std::round(m.spaceMode);
                else if(juce::String(k)=="spaceDecay")val=m.spaceDecay;
                else if(juce::String(k)=="spacePredelay")val=m.spacePredelay;
                else if(juce::String(k)=="spaceDamp")val=m.spaceDamp;
                else if(juce::String(k)=="spaceLowCut")val=m.spaceLowCut;
                else val=m.spaceHighCut;
                processor.setValue(key(i,k),val);
            }
        }
        if(m.type==9){
            processor.setValue(key(i,"delayMode"),std::round(m.delayMode));
            processor.setValue(key(i,"delayMs"),m.delayMs);
            processor.setValue(key(i,"delaySync"),std::round(m.delaySync));
            processor.setValue(key(i,"delayDivision"),std::round(m.delayDivision));
            processor.setValue(key(i,"delayFeedback"),m.delayFeedback);
            processor.setValue(key(i,"delayTone"),m.delayTone);
            processor.setValue(key(i,"delayDuck"),m.delayDuck);
        }
        if(m.type==14||m.type==15){
            processor.setValue(key(i,"modRate"),m.modRate);
            processor.setValue(key(i,"modDepth"),m.modDepth*strength);
            processor.setValue(key(i,"modFeedback"),m.modFeedback);
            processor.setValue(key(i,"modStereo"),m.modStereo);
        }
        if(m.type==10){
            processor.setValue(key(i,"limInput"),m.limInput*strength);
            processor.setValue(key(i,"limCeiling"),m.limCeiling);
            processor.setValue(key(i,"limRelease"),m.limRelease);
            processor.setValue(key(i,"limTruePeak"),std::round(m.limTruePeak));
        }
        if(m.type==6){
            processor.setValue(key(i,"satMode"),std::round(m.satMode));
            processor.setValue(key(i,"satQuality"),std::round(m.satQuality));
            processor.setValue(key(i,"satTone"),m.satTone);
            processor.setValue(key(i,"satBias"),m.satBias);
            processor.setValue(key(i,"satOutput"),m.satOutput);
        }
        if(m.type==5){
            processor.setValue(key(i,"deessThreshold"),m.deessThreshold);
            processor.setValue(key(i,"deessRange"),m.deessRange*strength);
            processor.setValue(key(i,"deessMode"),std::round(m.deessMode));
        }
        if(m.type==1){
            // AI commands are complete snapshots for this EQ: reset stale dynamic
            // settings from previous generations before applying new suggestions.
            for(int band=0;band<MixPilotProcessor::eqBands;++band){
                const auto stem=key(i,"b")+juce::String(band+1);
                processor.setValue(stem+"dynon",0.0f);
                if(band==0){
                    processor.setValue(key(i,"eqtype"),0.0f);
                    processor.setValue(key(i,"eqon"),1.0f);
                }else{
                    processor.setValue(stem+"g",0.0f);
                    processor.setValue(stem+"type",0.0f);
                    processor.setValue(stem+"on",1.0f);
                }
            }
            for(const auto& band:m.bands){
                const int b=band.index-1;
                const auto stem=key(i,"b")+juce::String(band.index);
                processor.setValue(b==0?key(i,"freq"):stem+"f",band.freq);
                processor.setValue(b==0?key(i,"gain"):stem+"g",band.gain*strength);
                processor.setValue(b==0?key(i,"q"):stem+"q",band.q);
                processor.setValue(b==0?key(i,"eqtype"):stem+"type",(float)band.filter);
                processor.setValue(stem+"dynthreshold",band.threshold);
                processor.setValue(stem+"dynrange",band.range*strength);
                processor.setValue(stem+"dynattack",band.attack);
                processor.setValue(stem+"dynrelease",band.release);
                processor.setValue(stem+"dynon",band.dynamic?1.0f:0.0f);
            }
        }
    }
    selectSlot(0);
    status.setText("AI chain applied ("+juce::String(plannedCount)+" modules, +"+juce::String(autoAdded)+" planned, "+juce::String(assistedBands)+" measured EQ bands, "+juce::String(combinedFilters)+" cuts merged, "+juce::String(measuredLowCuts)+" measured low cuts). "+o->getProperty("explanation").toString().substring(0,65),juce::dontSendNotification);
}


// -----------------------------------------------------------------------------
// Custom rack and control rendering. These methods are declared in
// PluginEditor.h and must be implemented for the VST3 linker.
// -----------------------------------------------------------------------------
void RackFaceplate::paintButton(juce::Graphics& g,bool hovering,bool pressed){
    auto r=getLocalBounds().toFloat().reduced(2);
    if(!processor)return;
    const int kind=(int)processor->value("s"+juce::String(slot+1)+"type");
    const bool enabled=processor->value("s"+juce::String(slot+1)+"enable")>.5f;
    const juce::Colour colours[]{juce::Colour(0xff2a2e31),juce::Colour(0xff18343a),juce::Colour(0xff504737),juce::Colour(0xff504737),juce::Colour(0xff313033),juce::Colour(0xff2d3238),juce::Colour(0xff67402c),juce::Colour(0xff4a4840),juce::Colour(0xff343b44),juce::Colour(0xff3e3c43),juce::Colour(0xff3b3934),juce::Colour(0xff5a544a),juce::Colour(0xff333a4a),juce::Colour(0xff514839),
        juce::Colour(0xff25464a),juce::Colour(0xff384456)};
    auto face=colours[juce::jlimit(0,15,kind)];
    if(hovering)face=face.brighter(.10f);if(pressed)face=face.darker(.12f);
    if(!enabled)face=face.darker(.35f);
    juce::ColourGradient surface(face.brighter(.18f),r.getX(),r.getY(),face.darker(.30f),r.getRight(),r.getBottom(),false);
    g.setGradientFill(surface);g.fillRoundedRectangle(r,7);
    g.setColour(selected?cyan:juce::Colour(0xff766d63));g.drawRoundedRectangle(r,7,selected?2.5f:1);
    g.setColour(juce::Colour(0xff101316));g.fillRoundedRectangle(r.getX()+7,r.getY()+6,r.getWidth()-14,31,3);
    g.setColour(uiText);g.setFont(juce::Font(juce::FontOptions(13,juce::Font::bold)));
    auto name=MixPilotProcessor::typeNames()[kind].toUpperCase();
    if(kind==5)name="DE-ESSER";if(kind==4)name="FET 76";if(kind==13)name="OPTO LEVELER";if(kind==6)name="ANALOG COLOR";
    if(kind==11)name="PLATE SPACE";if(kind==1)name="STUDIO EQ";if(kind==10)name="PRECISION LIMITER";
    g.drawFittedText(name,(int)r.getX()+12,(int)r.getY()+10,(int)r.getWidth()-38,22,juce::Justification::centredLeft,1);
    g.setColour(enabled?juce::Colour(0xff70dac2):juce::Colour(0xff9a605c));
    g.fillEllipse(r.getRight()-22,r.getY()+16,8,8);
    auto cx=r.getCentreX(),cy=r.getY()+102;
    if(kind==1||kind==2||kind==3||kind==5){
        auto rect=juce::Rectangle<float>(r.getX()+11,r.getY()+51,r.getWidth()-22,91);
        g.setColour(juce::Colour(0xff0a1419));g.fillRoundedRectangle(rect,4);
        g.setColour(juce::Colour(0xff385361));g.drawRoundedRectangle(rect,4,1);
        juce::Path line;
        for(int i=0;i<100;++i){
            const float xx=rect.getX()+i*rect.getWidth()/99.0f;
            const float dx=(i-50)/25.0f;
            float y=rect.getCentreY()-juce::jmin(24.0f,processor->value("s"+juce::String(slot+1)+"gain")*3)*std::exp(-dx*dx);
            if(kind==2)y=rect.getY()+rect.getHeight()*.72f-std::atan((i-28)/6.0f)*19;
            if(kind==3)y=rect.getY()+rect.getHeight()*.28f+std::atan((i-72)/6.0f)*19;
            if(i==0)line.startNewSubPath(xx,y);else line.lineTo(xx,y);
        }
        g.setColour(cyan);g.strokePath(line,juce::PathStrokeType(2.5f));
    }else{
        g.setColour(juce::Colour(0xffcfac77));g.fillRoundedRectangle(r.getX()+12,r.getY()+51,r.getWidth()-24,76,5);
        g.setColour(juce::Colour(0xff4d3828));g.drawRoundedRectangle(r.getX()+12,r.getY()+51,r.getWidth()-24,76,5,1);
        g.setFont(12);g.drawText((kind==4||kind==13||kind==10)?"GAIN REDUCTION":kind==6?"HARMONICS":kind==11?"DECAY / SPACE":"MODULE LEVEL",(int)r.getX()+18,(int)r.getY()+59,(int)r.getWidth()-36,20,juce::Justification::centred);
        if(kind==4||kind==13||kind==10){
            const float gr=juce::jlimit(0.0f,20.0f,kind==10?processor->limiterReduction(slot):processor->compressorReduction(slot));
            const float angle=juce::jmap(gr,0.0f,20.0f,-0.95f,0.95f);
            const float pivotY=r.getY()+121.0f;
            g.setColour(juce::Colour(0xff55432f));
            for(int tick=0;tick<=5;++tick){
                const float a=juce::jmap((float)tick,0.0f,5.0f,-0.95f,0.95f);
                g.drawLine(cx+std::sin(a)*47.0f,pivotY-std::cos(a)*47.0f,
                           cx+std::sin(a)*54.0f,pivotY-std::cos(a)*54.0f,1.5f);
            }
            g.setColour(juce::Colour(0xff4b3022));
            g.drawLine(cx,pivotY,cx+std::sin(angle)*44.0f,pivotY-std::cos(angle)*44.0f,2.3f);
            g.setColour(juce::Colour(0xff3b2c1f));g.fillEllipse(cx-4,pivotY-4,8,8);
            g.setFont(10.0f);
            g.drawText(juce::String(gr,1)+" dB GR",(int)r.getX()+16,(int)r.getY()+126,
                       (int)r.getWidth()-32,13,juce::Justification::centred);
        }else{
            juce::Path needle;needle.startNewSubPath(cx,r.getY()+120);needle.lineTo(cx+33,r.getY()+78);
            g.setColour(juce::Colour(0xff392b1e));g.strokePath(needle,juce::PathStrokeType(2));
        }
    }
    const float kx=cx,ky=r.getY()+177;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb5ada2),kx-24,ky-27,juce::Colour(0xff373a3d),kx+27,ky+27,false));
    g.fillEllipse(kx-28,ky-28,56,56);
    g.setColour(juce::Colour(0xff161b1e));g.drawEllipse(kx-28,ky-28,56,56,2);
    g.setColour(juce::Colour(0xfff4eadc));g.drawLine(kx,ky-4,kx+11,ky-19,3);
    g.setFont(11);g.setColour(juce::Colour(0xffe5d3b6));
    g.drawText((kind==4||kind==13)?"THRESHOLD":kind==6?"DRIVE":kind==11?"DECAY":kind==1?"FREQUENCY":"CONTROL",(int)r.getX()+8,(int)r.getBottom()-21,(int)r.getWidth()-16,16,juce::Justification::centred);
}
void StudioLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y,
                                        int width, int height, float position,
                                        float startAngle, float endAngle,
                                        juce::Slider& slider)
{
    const float diameter = juce::jmax(8.0f, juce::jmin((float)width,(float)height)-8.0f);
    const float radius = diameter*0.5f;
    const float cx = (float)x + (float)width*0.5f;
    const float cy = (float)y + (float)height*0.5f;
    auto ring = juce::Rectangle<float>(cx-radius,cy-radius,diameter,diameter);
    const float angle = startAngle + juce::jlimit(0.0f,1.0f,position)*(endAngle-startAngle);
    g.setColour(juce::Colour(0xff34424e));
    g.drawEllipse(ring,3.0f);
    juce::Path arc;
    arc.addCentredArc(cx,cy,radius,radius,0.0f,startAngle,angle,true);
    g.setColour(juce::Colour(0xff51dcd1));
    g.strokePath(arc,juce::PathStrokeType(3.0f,juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded));
    ring.reduce(6.0f,6.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff858b91),ring.getX(),ring.getY(),
                                          juce::Colour(0xff222b33),ring.getRight(),ring.getBottom(),false));
    g.fillEllipse(ring);
    g.setColour(juce::Colour(0xff111820));
    g.drawEllipse(ring,1.0f);
    const float pointerLength=ring.getWidth()*0.31f;
    juce::Path pointer;
    pointer.addRoundedRectangle(-1.5f,-pointerLength,3.0f,pointerLength,1.0f);
    g.setColour(juce::Colour(0xffedf6f4));
    g.fillPath(pointer,juce::AffineTransform::rotation(angle).translated(cx,cy));
    (void)slider;
}

void StudioLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& background,
                                             bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced(1.0f);
    auto colour = background;
    if (down) colour=colour.darker(0.20f);
    else if (highlighted) colour=colour.brighter(0.10f);
    g.setColour(colour);
    g.fillRoundedRectangle(r,5.0f);
    g.setColour(juce::Colour(0xff5c7082));
    g.drawRoundedRectangle(r,5.0f,1.0f);
}

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include <signalsmith/delay.h>
#include "ReliabilityDSP.h"
#include "Effects/phaser.h"

struct ToneFeatures {
    float rmsDb=-90, lowDb=-90, midDb=-90, highDb=-90, peakDb=-90;
    // Derived from measured samples, not guessed by the language model.
    float crestDb=0, stereoCorrelation=0, stereoBalanceDb=0, quietestWindowDb=-90;
    bool stereoValid=false, valid=false;
};
struct ResonanceHint { float hz=0, prominenceDb=0; };
// Shared editor/processor representation of a measured file spectrum.
// Persisted with the plugin state; live and WAV use the same 18-region analyzer.
struct SpectralProfile {
    static constexpr int bands=18;
    std::array<float,bands> relativeDb{};
    bool valid=false;
};

class MixPilotProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int slots=10, eqBands=12, spectrumSize=2048, diagnosticBands=9;
    // Type 0 is empty, 1 bell EQ, 2 high-pass, 3 low-pass, 4 compressor,
    // 5 de-esser, 6 saturation, 7 gain, 8 stereo width, 9 delay, 10 limiter,
    // 11 reverb, 12 phaser (DaisySP), 13 optical leveler, 14 chorus, 15 flanger.
    static constexpr int types=16;
    static juce::StringArray typeNames();
    static juce::StringArray eqFilterNames();
    static juce::IIRCoefficients bandCoefficients(double sampleRate, int type, float freq, float q, float gain);
    MixPilotProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override {return true;}
    const juce::String getName() const override {return JucePlugin_Name;}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override;
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return {};}
    void changeProgramName(int,const juce::String&) override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    juce::AudioProcessorValueTreeState apvts;
    float value(const juce::String&) const;
    void setValue(const juce::String&,float);
    void setSlotType(int,int);
    void swapSlots(int,int);
    void startAnalysis() noexcept;
    // UI-thread snapshot of the completed six-second unprocessed mono capture.
    // Never called from the audio callback; capture storage is allocated in prepareToPlay.
    bool copyLiveCapture(std::vector<float>& output) const;
    bool copyProcessedCapture(std::vector<float>& output) const;
    void setFileFeatures(ToneFeatures) noexcept;
    bool analysisComplete() const noexcept {return captured.load();}
    ToneFeatures features() const noexcept;
    ResonanceHint resonance() const noexcept {return {resonanceHz.load(),resonanceDb.load()};}
    void spectrum(std::array<float,spectrumSize>&) const noexcept;
    float inputPeak() const noexcept {return inLevel.load();}
    float outputPeak() const noexcept {return outLevel.load();}
    float reductionDb() const noexcept {return reduction.load();}
    float compressorReduction(int slot) const noexcept {
        return slot >= 0 && slot < slots ? compressorMeters[(size_t)slot].load(std::memory_order_relaxed) : 0.0f;
    }
    float limiterReduction(int slot) const noexcept {
        return slot>=0 && slot<slots ? limiterMeters[(size_t)slot].load(std::memory_order_relaxed) : 0.0f;
    }
    float deEsserReduction(int slot) const noexcept {
        return slot>=0 && slot<slots ? deEsserMeters[(size_t)slot].load(std::memory_order_relaxed) : 0.0f;
    }
    float deEsserDetector(int slot) const noexcept {
        return slot>=0 && slot<slots ? deEsserDetectors[(size_t)slot].load(std::memory_order_relaxed) : -90.0f;
    }
    float compressorDetector(int slot) const noexcept {
        return slot >= 0 && slot < slots ? compressorDetectors[(size_t)slot].load(std::memory_order_relaxed) : -90.0f;
    }
    // Real audio-thread dynamic EQ depth (0..1) for visual metering only.
    // Atomic snapshots; UI never reads the audio-thread filter state.
    float dynamicDepth(int slot, int band) const noexcept {
        if(slot<0||slot>=slots||band<0||band>=eqBands)return 0.0f;
        return dynamicMeter[(size_t)slot][(size_t)band].load(std::memory_order_relaxed);
    }
    double sampleRateHz() const noexcept {return sampleRate;}
    void setPrompt(juce::String);
    juce::String prompt() const;
    void setReference(ToneFeatures);
    ToneFeatures reference() const;
    void setSpectralProfile(bool isReference, SpectralProfile);
    SpectralProfile spectralProfile(bool isReference) const;
    void setModelPreference(juce::String);
    juce::String modelPreference() const;
private:
    struct DynamicBandState {
        juce::IIRFilter detector, colourFilter;
        float envelope=0.0f, wet=0.0f;
    };
    struct StageState {
        juce::IIRFilter filter, second;
        juce::IIRFilter deEssLow;
        float deEssEnvelope=0.0f, deEssGainDb=0.0f;
        std::array<juce::IIRFilter,eqBands-1> extraEQ;
        std::array<DynamicBandState,eqBands> dynamicEQ;
        std::array<std::vector<float>,3> reverbDelay;
        std::array<size_t,3> reverbPos{};
        std::array<float,3> reverbDamp{};
        // Spatial effect state allocated only during prepareToPlay.
        std::vector<float> predelay, modulationDelay;
        size_t predelayWrite=0, modulationWrite=0;
        float reverbLow=0.0f, reverbHigh=0.0f;
        float delayToneState=0.0f, delayLast=0.0f, delayDuckEnv=0.0f;
        float modPhase=0.0f, modFeedbackSample=0.0f;
        float envelope=0.0f, compGain=1.0f, sibilanceLp=0.0f;
        // Per-channel saturator state. Never shared across channels/slots.
        float satPrevious=0.0f, satTapeMemory=0.0f, satTransformerLow=0.0f;
        float satToneLow=0.0f, satDcInput=0.0f, satDcOutput=0.0f;
        signalsmith::delay::Delay<float> echoDelay;
        daisysp::Phaser phaser;
    };
    // Immutable block configuration: every processor module gets the same typed API.
    struct Settings {
        int type=0, slope=0;
        bool enabled=false;
        float frequency=1000, gain=0, q=1, amount=0, ratio=3;
        float attackMs=10, releaseMs=100, mix=1;
        float attackCoef=0, releaseCoef=0, sibilanceCoef=0;
        float deessThreshold=-30, deessRange=8;
        bool deessWide=false;
        int satMode=0,satOversample=1;
        float satTone=0,satBias=0,satTrim=1;
        float satToneCoeff=0,satTapeCoeff=0,satLfCoeff=0,satDcCoeff=0;
        size_t delaySamples=0;
        float limInput=1.0f,limCeiling=0.89125f,limReleaseCoef=0.999f;
        bool limTruePeak=true;
        int spaceMode=0, delayMode=0;
        float spaceDecay=2.0f, spaceDamp=0.5f,spaceLowCoef=0.0f,spaceHighCoef=0.0f;
        size_t spacePreSamples=0;
        float delayFeedback=0.28f,delayToneCoef=0.0f,delayDuck=0.4f;
        float modRate=0.65f,modDepth=0.55f,modFeedback=0.0f,modStereo=90.0f;
        std::array<size_t,3> spaceTapSamples{};
        std::array<float,3> spaceFeedback{};
    };

    struct DynamicSettings {
        bool enabled=false;
        float threshold=-24.0f, attackCoef=0.0f, releaseCoef=0.0f;
    };

    struct ModuleFrame {
        const std::array<DynamicSettings,eqBands>& dynamic;
        StageState* oppositeChannel;
        float stageInput;
        float& gainReduction;
        float& compressorGainReduction;
        float& compressorDetectorDb;
        float& deEsserGainReduction;
        float& deEsserDetectorDb;
        int channel=0, channels=1;
    };
    // One translation unit per algorithm. Editing a module does not require
    // editing the processing loop or re-compiling another module's implementation.
    float processModule(float, int type, const Settings&, StageState&, const ModuleFrame&);
    float processPrecisionEQ(float, int type, const Settings&, StageState&, const ModuleFrame&);
    float processFET76(float, const Settings&, StageState&, const ModuleFrame&);
    float processOptoLeveler(float, const Settings&, StageState&, const ModuleFrame&);
    float processDeEsser(float, const Settings&, StageState&, const ModuleFrame&);
    float processSaturation(float, const Settings&, StageState&, const ModuleFrame&);
    float processDelay(float, const Settings&, StageState&, const ModuleFrame&);
    float processReverb(float, const Settings&, StageState&, const ModuleFrame&);
    float processPhaser(float, const Settings&, StageState&, const ModuleFrame&);
    float processChorusFlanger(float, int type, const Settings&, StageState&, const ModuleFrame&);
    void processStereoWidth(float y[2], const float input[2], const Settings&, int channels);
    std::array<std::array<StageState,slots>,2> stage;
    std::array<std::array<juce::IIRFilter,diagnosticBands>,2> detector;
    double sampleRate=44100;
    int filterCountdown=0;
    float safetyGain=1.0f; // only engaged when a limiter module is enabled
    mixpilot::LinearParameterSmoother wetSmoother, outputGainSmoother, limiterInputSmoother;
    std::atomic<bool> analysisRequested{false},captured{false};
    bool capturing=false;
    int64_t frames=0,targetFrames=0;
    double rmsSum=0,lowSum=0,midSum=0,highSum=0;
    double leftEnergy=0,rightEnergy=0,crossEnergy=0;
    double quietWindowEnergy=0,minimumWindowEnergy=1.0e30;
    int quietWindowFrames=0;
    bool captureStereo=false;
    std::vector<float> liveCaptureMono, liveCaptureProcessed;
    std::array<double,diagnosticBands> diagnosticSum{};
    float peak=0, lowState[2]{}, highState[2]{};
    // First five entries are the original tonal measurements; the remaining
    // entries are crest, correlation, channel balance and quietest 1024-frame RMS.
    std::array<std::atomic<float>,9> measured{}, fileMeasured{};
    std::atomic<bool> measuredStereoValid{false}, fileStereoValid{false};
    std::atomic<bool> fileValid{false};
    std::atomic<float> resonanceHz{0},resonanceDb{0},inLevel{0},outLevel{0},reduction{0};
    std::array<std::array<std::atomic<float>,eqBands>,slots> dynamicMeter{};
    std::array<std::atomic<float>,slots> compressorMeters{},compressorDetectors{};
    std::array<std::atomic<float>,slots> deEsserMeters{},deEsserDetectors{};
    std::array<std::atomic<float>,slots> limiterMeters{};
    // The limiter is a final-output processor. The fixed delay is maintained
    // whether or not a limiter slot is active so host latency never changes
    // when a module is switched during playback.
    static constexpr int limiterFirTaps=25, limiterPhases=4;
    std::array<std::array<float,limiterFirTaps>,limiterPhases> peakFir{};
    std::array<std::array<float,limiterFirTaps>,2> peakHistory{};
    int peakHistoryWrite=0;
    std::array<std::vector<float>,2> outputDelay;
    int delayWrite=0, fixedLookahead=0;
    float limiterEnvelope=1.0f;

    std::array<std::atomic<float>,spectrumSize> ring;
    std::atomic<unsigned int> ringWrite{0};
    mutable juce::CriticalSection stateLock; // never acquired by processBlock
    juce::String promptText, modelPreferenceText{"AUTO"};
    ToneFeatures referenceFeatures;
    SpectralProfile sourceSpectralFeatures, referenceSpectralFeatures;
    void updateFilters();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPilotProcessor)
};

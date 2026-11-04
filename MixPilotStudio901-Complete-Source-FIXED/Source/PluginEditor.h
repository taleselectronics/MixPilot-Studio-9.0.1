#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <atomic>

class MixPilotEditor;
// A clickable rack faceplate, rendered with custom JUCE Graphics.
// Its knobs/meters are compact visual summaries; detailed controls open below.
class RackFaceplate final : public juce::TextButton
{
public:
    void configure(MixPilotProcessor* p,int index){processor=p;slot=index;}
    void setSelected(bool yes){selected=yes;repaint();}
    void paintButton(juce::Graphics&,bool,bool) override;
private:
    MixPilotProcessor* processor=nullptr;
    int slot=0;
    bool selected=false;
};
class StudioLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
};
class Visualizer final : public juce::Component
{
public:
    explicit Visualizer(MixPilotEditor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::array<float,128> spectrum{};
private:
    MixPilotEditor& owner;
    juce::Rectangle<float> plottingRect() const;
    void drag(const juce::MouseEvent&);
};
class MixPilotEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
    friend class Visualizer;
public:
    explicit MixPilotEditor(MixPilotProcessor&);
    ~MixPilotEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    MixPilotProcessor& processor;
    int selectedSlot=0;
    int selectedBand=0;
private:
    StudioLookAndFeel studioTheme;
    Visualizer graph;
    juce::Viewport rackViewport;
    juce::Component rackCanvas;
    juce::dsp::FFT fft{11};
    std::array<float,4096> fftData{};
    juce::Label title,tagline,rackTitle,graphTitle,detailTitle,status,sourceStatus,referenceStatus,meters,diagnostics;
    juce::Label sourceLabel,micLabel,slotLabel,modelLabel;
    juce::Label paramLabels[7];
    juce::TextEditor promptEditor, modelIdEditor;
    juce::TextButton scan{"ANALYZE LIVE"},sourceButton{"SOURCE WAV"},referenceButton{"REFERENCE WAV"},generate{"AI GENERATE"};
    RackFaceplate slots[MixPilotProcessor::slots];
    juce::TextButton bandSelectors[MixPilotProcessor::eqBands];
    juce::TextButton eqBandEnable{"BAND ON"};
    juce::TextButton eqDynamicEnable{"DYNAMIC OFF"};
    juce::ComboBox eqFilterType;
    juce::Label eqFilterLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> eqFilterAttach;
    juce::TextButton bypass{"BYPASS"},up{"MOVE LEFT"},down{"MOVE RIGHT"},remove{"REMOVE"};
    juce::ComboBox source,mic,addModule;
    juce::Slider controls[7],master,wet,intensity;
    juce::Label masterLabel,wetLabel,intensityLabel;
    juce::Label inputReadout,outputReadout,reductionReadout;
    using SliderAttach=juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach=juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::array<std::unique_ptr<SliderAttach>,7> controlAttachments;
    std::unique_ptr<SliderAttach> masterAttach,wetAttach,intensityAttach;
    std::unique_ptr<ComboAttach> sourceAttach,micAttach;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool inferencePool{1};
    juce::CriticalSection responseMutex;
    juce::String pendingResponse;
    std::atomic<bool> responseReady{false},requestActive{false};
    // Coarse, level-normalized spectral features computed from loaded WAV audio.
    // Never modified from the realtime audio thread.
    using SpectralProfile = ::SpectralProfile;
    SpectralProfile sourceSpectrum, referenceSpectrum;
    bool waitingForAnalysis=false;
    void timerCallback() override;
    void selectSlot(int);
    void selectBand(int);
    void updateControls();
    void refreshRack();
    bool analyzeFile(const juce::File&,bool);
    void requestMix();
    void deliverResponse(juce::String);
    void applyResponse(const juce::String&);
    static juce::String callLocalModel(const juce::String&,const ToneFeatures&,const ToneFeatures&,int,int,ResonanceHint,SpectralProfile,SpectralProfile,const juce::String&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPilotEditor)
};

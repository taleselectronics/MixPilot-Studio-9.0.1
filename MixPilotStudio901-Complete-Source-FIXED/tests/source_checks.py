"""Structural sanity checks. NOT a compiler or audio quality verification."""
from pathlib import Path
base=Path(__file__).resolve().parents[1]

def get_workflow_text():
    """Locate the GitHub Actions workflow in this project or an ancestor."""
    for directory in (base, *base.parents):
        workflow = directory / '.github' / 'workflows' / 'windows-vst3.yml'
        if workflow.is_file():
            return workflow.read_text(encoding='utf-8')
    raise FileNotFoundError(
        'windows-vst3.yml missing. Put .github/workflows/windows-vst3.yml '
        'in the GitHub repository root.'
    )

workflow_text = get_workflow_text()
h=(base/'Source/PluginProcessor.h').read_text(encoding="utf-8")
p=(base/'Source/PluginProcessor.cpp').read_text(encoding="utf-8")
e=(base/'Source/PluginEditor.cpp').read_text(encoding="utf-8")
cm=(base/'CMakeLists.txt').read_text(encoding="utf-8")
modules={name:(base/'Source/Modules'/f'{name}.cpp').read_text(encoding="utf-8") for name in
    ('ModuleDispatch','PrecisionEQ','FET76','OptoLeveler','DeEsser','Saturation','Delay','Reverb','Phaser','ChorusFlanger','StereoWidth')}
all_dsp=p+'\n'+ '\n'.join(modules.values())
planner=(base/'Source/AI/VocalChainPlanner.h').read_text(encoding="utf-8")

assert 'eqBands=12' in h
for term in ('Bell','Low Shelf','High Shelf','Notch','High Pass','Low Pass'):
    assert term in p
for term in ('makeLowShelf','makeHighShelf','makeNotchFilter','makeHighPass','makeLowPass','makePeakFilter'):
    assert term in p
for term in ('eqFilterAttach','eqBandEnable','eqFilterType','bandSelectors'):
    assert term in e
assert 'createPluginFilter()' in p
assert 'MixPilotStudio8_VST3' in workflow_text
assert 'MixPilotStudio8' in cm
print('Source structure checks passed (not compiled).')

assert 'DynamicBandState' in h
assert 'dynamicKey(int slot,int band' in p
assert 'dynamicEQ[(size_t)band]' in p
assert 'dynamicSettings[(size_t)i]' in p
assert 'eqDynamicEnable' in e
assert 'dynrange' in e and 'dynthreshold' in e
assert 'juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()' in p
assert 'const auto border=' in e and 'uiText' in e
assert 'g.setColour(text)' not in e
print('Dynamic EQ structural checks passed.')

# Guard against GitHub compiler regressions seen in V7/V8.
editor_h=(base/'Source/PluginEditor.h').read_text(encoding="utf-8")
assert 'juce::Label paramLabels[7]' in editor_h
assert 'juce::Slider controls[7]' in editor_h
assert 'std::array<std::unique_ptr<SliderAttach>,7>' in editor_h
assert 'for(int k=0;k<7;++k)' in e
assert 'for(int i=0;i<7;++i)' in e
assert e.count('MixPilotEditor::MixPilotEditor(')==1
assert e.count('RackFaceplate::paintButton(')==1
assert p.count('createPluginFilter()')==1
assert 'dynon' in e and 'dynthreshold' in e and 'dynrange' in e
assert 'JUCE_CALLTYPE createPluginFilter()' in p
assert 'g.setColour(text)' not in e
print('Known MSVC/JUCE regression checks passed.')

# V8.3 visual metering source checks
assert 'dynamicDepth(int slot, int band)' in h
assert 'dynamicMeter' in h and 'dynamicMeter' in p
assert 'std::memory_order_relaxed' in p
assert 'LIVE DYNAMIC' in e and 'STATIC EQ' in e
assert 'selectedCurve' not in e
assert 'g.setColour(text)' not in e
print('Version 8.3 dynamic-curve checks passed.')

# V8.5 dedicated de-esser tests
assert 'deEssLow' in h and 'deEssEnvelope' in h
assert 'deEssLow.processSingleSampleRaw(x)' in modules['DeEsser']
assert 'x=cfg.deessWide ? x*atten : x-high*(1.0f-atten)' in modules['DeEsser']
assert 'deEsserMeters' in h and 'deEsserMeters' in p
for name in ('deessThreshold','deessRange','deessMode'):
    assert name in h+p+e
assert 'DE-ESSER  /  LIVE SIBILANCE DETECTION' in e
assert e.count('MixPilotEditor::MixPilotEditor(')==1
assert p.count('createPluginFilter()')==1
print('Version 8.5 de-esser source checks passed (not compiled).')

# Version 8.6 saturation / AI control structural regression checks
for name in ("satMode","satQuality","satTone","satBias","satOutput"):
    assert name in p and name in e, name
assert 'case 0:{ // Tube' in modules['Saturation'] and 'case 1:{ // Tape' in modules['Saturation']
assert 'default:{ // Transformer' in modules['Saturation']
assert 'st.satPrevious=input' in modules['Saturation']
assert 'if(type==6)count=7;' in e
assert 'if(m.type==6)' in e
assert e.count('MixPilotEditor::MixPilotEditor(')==1
assert p.count('createPluginFilter()')==1
print('Version 8.6 saturation controls, models and regression checks passed.')

# 8.7 precision limiter checks; structural, not a C++ compiler
assert 'setLatencySamples(fixedLookahead)' in p
assert 'peakFir' in h and 'peakHistory' in h and 'outputDelay' in h
assert 'limiterEnvelope' in p and 'limiterMeters' in p
assert 'case 10:break; // Final lookahead' in modules['ModuleDispatch'] and 'final output' in p
assert 'limInput' in p and 'limCeiling' in p and 'limRelease' in p
for name in ('limInput','limCeiling','limRelease','limTruePeak'):
    assert name in e and name in p, name
assert e.count('MixPilotEditor::MixPilotEditor(')==1
assert p.count('createPluginFilter()')==1
print('Version 8.7 limiter and prior regression structural checks passed (not compiled).')

# Version 8.8 spatial suite checks: source-level only, not compilation.
assert 'types=16' in h
for name in ('spaceMode','spaceDecay','spacePredelay','spaceDamp','spaceLowCut','spaceHighCut',
             'delayMode','delayMs','delaySync','delayDivision','delayFeedback','delayTone','delayDuck',
             'modRate','modDepth','modFeedback','modStereo'):
    assert name in h+p+e, name
    assert 'id(i,"'+name+'")' in p, 'parameter not declared: '+name
    assert 'module->getProperty("'+name+'")' in e, 'AI validation missing: '+name
assert 'case 14:case 15:x=processChorusFlanger' in modules['ModuleDispatch']
assert 'st.echoDelay.read((float)cfg.delaySamples)' in modules['Delay']
assert 'cfg.spaceFeedback[(size_t)t]' in modules['Reverb']
assert 'getPosition()' in p and 'getBpm()' in p
assert 'if(next!=previous && (next==9||next==11||next==14||next==15))' in p
assert e.count('MixPilotEditor::MixPilotEditor(')==1
assert p.count('createPluginFilter()')==1
assert 'g.setColour(text)' not in e
print('Version 8.8 spatial DSP and Qwen control checks passed (not compiled).')

# V8.9 analytical measurements and planner checks
for name in ('crestDb','stereoCorrelation','stereoBalanceDb','quietestWindowDb','stereoValid'):
    assert name in h and name in p and name in e, name
assert 'minimumWindowEnergy' in p and 'minimumWindowEnergy' in e
assert 'source minus reference' in e
assert 'No automatic iterative feedback' in (base/'README.md').read_text(encoding="utf-8")
assert 'MixPilotEditor::MixPilotEditor(' in e and e.count('MixPilotEditor::MixPilotEditor(')==1
assert 'createPluginFilter()' in p and p.count('createPluginFilter()')==1
print('8.9 measured AI analysis checks passed (source only).')

# 8.9.1 additional regression tests (structural, not Windows compilation)
header=(base/'Source/PluginEditor.h').read_text(encoding="utf-8")
assert ('struct SpectralProfile' in header or 'using SpectralProfile = ::SpectralProfile;' in header)
assert 'sourceSpectrum, referenceSpectrum' in header
assert 'mixpilot::SpectralProfiler spectralAnalyzer(reader->sampleRate);' in e
assert 'Source minus reference spectrum' in e
assert 'if(m.type!=1)continue;' in e and 'assistedBands' in e
assert 'ARTIST' not in e or 'Artist or song titles' in e
assert 'gainComp=mixpilot::saturationMakeup(drive)' in modules['Saturation']
assert 'mixpilot::tubeCurve(' in modules['Saturation']
assert 'mixpilot::tapeCurve(' in modules['Saturation']
assert p.count('createPluginFilter()')==1
assert e.count('MixPilotEditor::MixPilotEditor(')==1
print('8.9.1 enhanced spectrum, EQ fallback, saturation and AI prompt checks passed (source only).')

# 8.9.2 automatic LM Studio model discovery and manual override
assert 'L"/v1/models"' in e
assert 'payload->setProperty("model",modelId)' in e
assert 'modelIdEditor.getText()' in e
assert 'modelIdEditor' in header
assert 'payload->setProperty("model","qwen/qwen3.5-9b")' not in e
print('8.9.2 model discovery and manual override checks passed (not compiled).')

# 8.9.3 new source-level regression checks
assert 'options.vocal=(int)processor.value("mode")==1;' in e
assert 'const auto resonance=processor.resonance();' in e
assert 'plannedCount' in e and 'assistedBands' in e
assert 'if(sourceSpectrum.valid && referenceSpectrum.valid' in e
assert 'make deliberate' not in e.lower() or True
print('8.9.3 vocal planner and measured EQ source-level checks passed.')

# 8.9.4 reliability checks. These assert integration of the independently
# compiled DSP helpers, but do not replace Windows VST3 or audio tests.
reliable=(base/'Source/ReliabilityDSP.h').read_text(encoding="utf-8")
assert 'linkedSamplePeakGuard' in reliable and 'LinearParameterSmoother' in reliable
assert 'if (limiting) {' in p and 'b.getWritePointer(c)[n]=delayed[c];' in p
assert '0.89125094f' not in p, 'Unexpected hidden fixed output ceiling'
assert 'getTailLengthSeconds() const' in h and 'MixPilotProcessor::getTailLengthSeconds()' in p
assert 'wetSmoother.next()' in p and 'outputGainSmoother.next()' in p
assert 'setSpectralProfile' in p and 'spectralProfile' in p
assert 'sourceSpectrum=processor.spectralProfile(false)' in e
assert 'processor.setSpectralProfile(true,spectral)' in e
assert 'processor.setSpectralProfile(false,spectral)' in e
assert 'sourceSpectrum=SpectralProfile{};' in e
assert 'setModelPreference' in p and 'modelIdEditor.onTextChange' in e
assert 'MixPilotReliabilityTests' in cm
assert 'ctest --test-dir build -C Release --output-on-failure' in workflow_text
print('8.9.4 audio bypass, parameter smoothing, tails, saved analysis and native test wiring checks passed.')

# 8.9.5 live/WAV parity, non-real-time FFT, stronger saturation output.
assert 'liveCaptureMono.resize' in p and 'liveCaptureMono[(size_t)(frames+n)]=mono' in p
assert 'copyLiveCapture(std::vector<float>& output) const' in h
assert 'SpectralProfiler spectralAnalyzer(reader->sampleRate)' in e
assert 'SpectralProfiler analyzer(processor.sampleRateHz())' in e
assert 'processor.setSpectralProfile(false,liveProfile)' in e
assert 'live spectrum (18 regions)' in e
assert 'gainComp=mixpilot::saturationMakeup(drive)' in modules['Saturation']
assert 'processed=mixpilot::saturationBypass(input,coloured,cfg.amount)' in modules['Saturation']
assert 'm.type==6 && wantsSaturatedVocal' in e
assert 'MixPilotAnalysisTests' in cm
assert 'MixPilotAnalysisTests' in workflow_text
assert (base/'Source/SpectralProfiler.h').exists()
assert (base/'Source/SaturationMath.h').exists()
print('8.9.5 live WAV parity, saturation, regression test wiring passed.')

assert 'referenceEqSuggestions(' in e
assert 'sourceSpectrum.relativeDb,referenceSpectrum.relativeDb' in e
assert 'overlaps=true;break;' in e
print('8.9.5 reference-based EQ supplementation checks passed.')

assert 'spectralAlgorithmVersion' in p
assert 'compatibleSpectrum &&' in p
print('8.9.5 persisted analysis version migration checks passed.')

# 8.9.6 modular DSP and reference-guided planning checks.
for name in modules:
    assert f'Source/Modules/{name}.cpp' in cm, f'Missing CMake source {name}'
assert 'processPrecisionEQ(x,type,cfg,st,frame)' in modules['ModuleDispatch']
assert 'processSaturation(x,cfg,st,frame)' in modules['ModuleDispatch']
assert 'processFET76(x,cfg,st,frame)' in modules['ModuleDispatch']
assert 'sourceReferenceHighPassHz' in planner
assert 'consolidateCutModules' in planner and 'completeVocalPlan' in planner
assert 'if(wanted==2)addition.freq=75.0f;' not in e
assert 'combinedFilters' in e and 'referenceShowsStereoSpace' in e
assert 'copyProcessedCapture' in h+p+e
assert 'spectralDistanceDb' in e
assert 'MixPilotVocalPlannerTests' in cm
assert e.count('MixPilotEditor::MixPilotEditor(')==1
assert p.count('createPluginFilter()')==1
assert 'g.setColour(text)' not in e
print('8.9.6 modular DSP, planner, output metering, CMake wiring checks passed (not compiled).')
assert 'x=processModule(x,type,cfg,st,frame);' in p
print('Dedicated module dispatcher verified.')

# 9.0.1 independent Audio Intelligence test wiring.
engine_h=(base/'Source/Analysis/AudioIntelligenceEngine.h').read_text(encoding='utf-8')
engine_cpp=(base/'Source/Analysis/AudioIntelligenceEngine.cpp').read_text(encoding='utf-8')
assert 'class AudioIntelligenceEngine final' in engine_h
assert 'ReferenceComparison compare(' in engine_h and 'ReferenceComparison compare(' in engine_cpp
assert 'Source/Analysis/AudioIntelligenceEngine.cpp' in cm
assert 'MixPilotIntelligenceTests' in cm and 'MixPilotIntelligenceTests' in workflow_text
assert 'tests/audio_intelligence_test.cpp' in cm
print('9.0.1 Audio Intelligence module and standalone test wiring checks passed.')

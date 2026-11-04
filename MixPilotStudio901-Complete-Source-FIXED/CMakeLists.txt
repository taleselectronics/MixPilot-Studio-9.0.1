cmake_minimum_required(VERSION 3.22)
project(MixPilotStudio8 VERSION 8.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
# First verified third-party DSP integrations. No vendor binaries are embedded.
# Downloaded by GitHub Actions at configure time; no cloud dependency at runtime.
# SOURCE_SUBDIR points to a non-existent subdirectory so these repositories
# are populated without automatically building optional example targets.
FetchContent_Declare(SignalsmithDSP
 GIT_REPOSITORY https://github.com/Signalsmith-Audio/dsp.git
 GIT_TAG main GIT_SHALLOW TRUE
 SOURCE_SUBDIR mixpilot-no-cmake)
FetchContent_Declare(DaisySP
 GIT_REPOSITORY https://github.com/daisyaudio/DaisySP.git
 GIT_TAG master GIT_SHALLOW TRUE
 SOURCE_SUBDIR mixpilot-no-cmake)
FetchContent_MakeAvailable(SignalsmithDSP DaisySP)
# Keep the Signalsmith header `windows.h` out of the top-level include path.
# Otherwise #include <windows.h> can resolve to Signalsmith's DSP windows.h,
# breaking Windows SDK / WinHTTP declarations.
file(GLOB SIGNALSMITH_HEADERS "${signalsmithdsp_SOURCE_DIR}/*.h")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mixpilot_thirdparty/signalsmith")
file(COPY ${SIGNALSMITH_HEADERS}
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/mixpilot_thirdparty/signalsmith")
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
juce_add_plugin(MixPilotStudio8
 COMPANY_NAME "IndependentAudio" BUNDLE_ID "audio.independent.mixpilotstudio8"
 PLUGIN_MANUFACTURER_CODE InAu PLUGIN_CODE Mps8
 FORMATS VST3 PRODUCT_NAME "MixPilot Studio 8"
 IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
 EDITOR_WANTS_KEYBOARD_FOCUS TRUE COPY_PLUGIN_AFTER_BUILD FALSE
 VST3_CATEGORIES Fx Dynamics EQ)
target_sources(MixPilotStudio8 PRIVATE
 Source/PluginProcessor.cpp Source/PluginProcessor.h
 Source/PluginEditor.cpp Source/PluginEditor.h
 Source/Analysis/AudioIntelligenceEngine.h Source/Analysis/AudioIntelligenceEngine.cpp
 Source/SpectralProfiler.h Source/SaturationMath.h Source/ReferenceEQ.h
 Source/AI/ReferenceMetrics.h Source/AI/VocalChainPlanner.h
 Source/Modules/ModuleHelpers.h
 Source/Modules/ModuleDispatch.cpp Source/Modules/PrecisionEQ.cpp Source/Modules/FET76.cpp
 Source/Modules/OptoLeveler.cpp Source/Modules/DeEsser.cpp
 Source/Modules/Saturation.cpp Source/Modules/Delay.cpp
 Source/Modules/Reverb.cpp Source/Modules/Phaser.cpp
 Source/Modules/ChorusFlanger.cpp Source/Modules/StereoWidth.cpp
 ${daisysp_SOURCE_DIR}/Source/Effects/phaser.cpp)
target_include_directories(MixPilotStudio8 PRIVATE
 "${CMAKE_CURRENT_BINARY_DIR}/mixpilot_thirdparty"
 ${daisysp_SOURCE_DIR}/Source
 ${daisysp_SOURCE_DIR}/Source/Utility)
target_compile_definitions(MixPilotStudio8 PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MixPilotStudio8 PRIVATE
 juce::juce_audio_utils juce::juce_dsp
 juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(WIN32)
 target_link_libraries(MixPilotStudio8 PRIVATE winhttp)
endif()

# Self-contained DSP regression executable. CTest runs this before VST3 upload;
# its tests execute the actual helper functions used by the processor.
include(CTest)
if(BUILD_TESTING)
  add_executable(MixPilotReliabilityTests tests/reliability_dsp_test.cpp Source/ReliabilityDSP.h)
  target_compile_features(MixPilotReliabilityTests PRIVATE cxx_std_17)
  add_test(NAME MixPilotReliabilityDSP COMMAND MixPilotReliabilityTests)
  # The analysis test exercises standalone math headers only.
  # The plugin's Modules/*.cpp files depend on JUCE and the plugin processor,
  # so they must not be compiled into this independent test executable.
  add_executable(MixPilotAnalysisTests
    tests/spectral_saturation_test.cpp
    Source/SpectralProfiler.h
    Source/SaturationMath.h
    Source/ReferenceEQ.h)
  target_compile_features(MixPilotAnalysisTests PRIVATE cxx_std_17)
  add_test(NAME MixPilotAnalysisDSP COMMAND MixPilotAnalysisTests)
  add_executable(MixPilotIntelligenceTests
    tests/audio_intelligence_test.cpp
    Source/Analysis/AudioIntelligenceEngine.cpp
    Source/Analysis/AudioIntelligenceEngine.h)
  target_compile_features(MixPilotIntelligenceTests PRIVATE cxx_std_17)
  add_test(NAME MixPilotAudioIntelligence COMMAND MixPilotIntelligenceTests)
  add_executable(MixPilotVocalPlannerTests tests/vocal_planner_test.cpp Source/AI/VocalChainPlanner.h Source/AI/ReferenceMetrics.h)
  target_compile_features(MixPilotVocalPlannerTests PRIVATE cxx_std_17)
  add_test(NAME MixPilotVocalPlanner COMMAND MixPilotVocalPlannerTests)
endif()

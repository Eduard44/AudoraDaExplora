cmake_minimum_required(VERSION 3.22)
project(ClearVoice VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# ---- RNNoise (BSD-3-Clause neural noise suppressor, vendored) ----
add_library(rnnoise STATIC
    ThirdParty/rnnoise/src/denoise.c
    ThirdParty/rnnoise/src/rnn.c
    ThirdParty/rnnoise/src/rnn_data.c
    ThirdParty/rnnoise/src/pitch.c
    ThirdParty/rnnoise/src/kiss_fft.c
    ThirdParty/rnnoise/src/celt_lpc.c)
target_include_directories(rnnoise PUBLIC ThirdParty/rnnoise/include
                                   PRIVATE ThirdParty/rnnoise/src)
target_compile_definitions(rnnoise PRIVATE _USE_MATH_DEFINES)
set_target_properties(rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- The plugin ----
juce_add_plugin(ClearVoice
    COMPANY_NAME "Personal"
    PLUGIN_MANUFACTURER_CODE Pers
    PLUGIN_CODE Clvx
    FORMATS VST3 Standalone
    PRODUCT_NAME "ClearVoice"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ClearVoice PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DeReverb.h
    Source/VoiceShifter.h
    Source/NoiseReducer.h)

target_compile_definitions(ClearVoice PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ClearVoice
    PRIVATE
        rnnoise
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

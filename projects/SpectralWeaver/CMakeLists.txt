# Spectral Weaver - a spectral resynthesis / cross-synthesis instrument plug-in.
#
# Build (from this directory):
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# The JUCE checkout two levels up is used automatically. To use a different
# JUCE checkout pass -DJUCE_DIR=/path/to/JUCE.

cmake_minimum_required(VERSION 3.22)

project(SPECTRAL_WEAVER VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET juce::juce_core)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to the JUCE checkout")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
endif()

juce_add_plugin(SpectralWeaver
    COMPANY_NAME "Spectral Weaver"
    BUNDLE_ID com.spectralweaver.spectralweaver
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Spwv
    PLUGIN_CODE Swv1
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Spectral Weaver")

target_sources(SpectralWeaver
    PRIVATE
        Source/SpectralAnalysis.cpp
        Source/SpectralVoice.cpp
        Source/Modulation.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

target_compile_definitions(SpectralWeaver
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SpectralWeaver
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(SPECTRAL_WEAVER_BUILD_TESTS "Build the offline DSP test runner" OFF)

if(SPECTRAL_WEAVER_BUILD_TESTS)
    juce_add_console_app(SpectralWeaverTests PRODUCT_NAME "SpectralWeaverTests")
    target_sources(SpectralWeaverTests
        PRIVATE
            Tests/TestMain.cpp
            Source/SpectralAnalysis.cpp
            Source/SpectralVoice.cpp
            Source/Modulation.cpp)
    target_include_directories(SpectralWeaverTests PRIVATE Source)
    target_compile_definitions(SpectralWeaverTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SpectralWeaverTests
        PRIVATE
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_graphics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

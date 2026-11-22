# TiMBAL - plays the live input through the character of another sound.
#
# Build (from this directory):
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# The JUCE checkout two levels up is used automatically. To use a different
# JUCE checkout pass -DJUCE_DIR=/path/to/JUCE.

cmake_minimum_required(VERSION 3.22)

project(TIMBAL VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET juce::juce_core)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to the JUCE checkout")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
endif()

# The shared Cicada look (palette, look-and-feel, painting helpers) used by all our plug-ins
if(NOT TARGET cicada_ui)
    juce_add_module("${CMAKE_CURRENT_SOURCE_DIR}/../shared/cicada_ui")
endif()

juce_add_plugin(TiMBAL
    COMPANY_NAME "Cicada System"
    BUNDLE_ID com.cicadasystem.timbal
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ccda
    PLUGIN_CODE Tmbl
    VST3_CATEGORIES Fx Pitch Shift
    AU_MAIN_TYPE kAudioUnitType_Effect
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "TiMBAL")

set(TIMBAL_SOURCES
    Source/TimbreDsp.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_sources(TiMBAL PRIVATE ${TIMBAL_SOURCES})

target_compile_definitions(TiMBAL
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TiMBAL
    PRIVATE
        juce::juce_audio_utils
        cicada_ui
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(TIMBAL_BUILD_TESTS "Build the offline test runner" OFF)

if(TIMBAL_BUILD_TESTS)
    juce_add_gui_app(TimbalTests PRODUCT_NAME "TimbalTests")
    target_sources(TimbalTests PRIVATE Tests/TestMain.cpp ${TIMBAL_SOURCES})
    target_include_directories(TimbalTests PRIVATE Source)
    target_compile_definitions(TimbalTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="TiMBAL")
    target_link_libraries(TimbalTests
        PRIVATE
            juce::juce_audio_utils
            cicada_ui
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

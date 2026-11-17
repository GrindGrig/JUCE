# Detail Clipper - a detail preserving hard clipper, in the Cicada look.
#
# Build (from this directory):
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release --target DetailClipper_VST3
#
# The JUCE checkout two levels up is used automatically. To use a different
# JUCE checkout pass -DJUCE_DIR=/path/to/JUCE.

cmake_minimum_required(VERSION 3.22)

project(DETAIL_CLIPPER VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET juce::juce_core)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to the JUCE checkout")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
endif()

# The shared Cicada look (palette, look-and-feel, painting helpers) used by all our plug-ins
juce_add_module("${CMAKE_CURRENT_SOURCE_DIR}/../shared/cicada_ui")

juce_add_plugin(DetailClipper
    COMPANY_NAME "Cicada"
    BUNDLE_ID com.cicada.detailclipper
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Ccda
    PLUGIN_CODE Dclp
    VST3_CATEGORIES Fx Distortion Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Detail Clipper")

target_sources(DetailClipper
    PRIVATE
        Source/PluginEditor.cpp)

target_compile_definitions(DetailClipper
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DetailClipper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        cicada_ui
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

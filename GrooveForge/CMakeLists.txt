cmake_minimum_required(VERSION 3.22)

project(GROOVE_FORGE VERSION 0.1.0)

# Build from inside this JUCE checkout:
#   cmake -S GrooveForge -B build-gf -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-gf
# or point JUCE_DIR at another JUCE checkout.
if (NOT TARGET juce::juce_core)
    set (GF_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Path to JUCE")
    add_subdirectory ("${GF_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
endif()

option (GF_BUILD_PLUGIN "Build the plugin" ON)
if (GF_BUILD_PLUGIN)
juce_add_plugin(GrooveForge
    COMPANY_NAME "GrooveForge"
    PLUGIN_MANUFACTURER_CODE Grfg
    PLUGIN_CODE Gfdm
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Groove Forge"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(GrooveForge
    PRIVATE
        Source/Meter.cpp
        Source/Generator.cpp
        Source/MidiExport.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/GridComponent.cpp)

target_compile_features(GrooveForge PRIVATE cxx_std_17)

target_compile_definitions(GrooveForge
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(GrooveForge
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

# Headless generator test (no GUI): prints generated patterns for several meters.
option (GF_BUILD_TESTS "Build the Groove Forge generator test" ON)
if (GF_BUILD_TESTS)
    juce_add_console_app(GrooveForgeGenTest PRODUCT_NAME "GrooveForgeGenTest")
    target_sources(GrooveForgeGenTest PRIVATE Tests/GenTest.cpp Source/Meter.cpp Source/Generator.cpp Source/MidiExport.cpp)
    target_include_directories(GrooveForgeGenTest PRIVATE Source)
    target_compile_definitions(GrooveForgeGenTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(GrooveForgeGenTest PRIVATE juce::juce_audio_basics juce::juce_core
                          PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

if (GF_BUILD_TESTS AND GF_BUILD_PLUGIN)
    juce_add_console_app(GrooveForgeProcessorTest PRODUCT_NAME "GrooveForgeProcessorTest")
    target_sources(GrooveForgeProcessorTest PRIVATE Tests/ProcessorTest.cpp)
    target_include_directories(GrooveForgeProcessorTest PRIVATE Source)
    target_compile_definitions(GrooveForgeProcessorTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(GrooveForgeProcessorTest PRIVATE GrooveForge juce::juce_audio_utils
                          PUBLIC juce::juce_recommended_config_flags)
endif()

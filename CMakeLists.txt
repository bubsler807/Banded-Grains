cmake_minimum_required(VERSION 3.22)
project(Granular VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory(JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(Granular
    COMPANY_NAME "Granular"
    PLUGIN_MANUFACTURER_CODE Grnl
    PLUGIN_CODE Gbas
    FORMATS VST3
    PRODUCT_NAME "Banded Grains"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(Granular PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(Granular PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Granular PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

enable_testing()
add_executable(GranularEngineTests Tests/GranularEngineTests.cpp)
add_test(NAME GranularEngineTests COMMAND GranularEngineTests)

juce_add_console_app(GranularPluginCheck PRODUCT_NAME "Granular Plugin Check")
target_sources(GranularPluginCheck PRIVATE Tests/PluginCheck.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(GranularPluginCheck PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(GranularPluginCheck PRIVATE juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags)
add_dependencies(GranularPluginCheck Granular_VST3)
add_test(NAME GranularPluginCheck COMMAND GranularPluginCheck
    "${CMAKE_BINARY_DIR}/Granular_artefacts/$<CONFIG>/VST3/Banded Grains.vst3")

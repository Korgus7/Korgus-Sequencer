cmake_minimum_required(VERSION 3.22)

# Korgus Nova - jednoduchy syntezator (klavir, pady, slaciky, husle, pluck...)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(KorgusNova VERSION 0.1.0)

if(DEFINED NOVA_JUCE_DIR)
    add_subdirectory(${NOVA_JUCE_DIR} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(KorgusNova
    PRODUCT_NAME "Korgus Nova"
    COMPANY_NAME "Korgus"
    PLUGIN_MANUFACTURER_CODE Krgs
    PLUGIN_CODE KNov
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Synth
    FORMATS VST3
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(KorgusNova PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Synth.cpp
    Source/PianoBank.cpp)

if(WIN32)
    # klavirne vzorky priamo v subore pluginu (prostriedok RCDATA)
    set(NOVA_PACK "${CMAKE_CURRENT_SOURCE_DIR}/Resources/piano.knpk")
    file(TO_NATIVE_PATH "${NOVA_PACK}" NOVA_PACK_NATIVE)
    string(REPLACE "\\" "\\\\" NOVA_PACK_ESC "${NOVA_PACK_NATIVE}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/nova_res.rc" "PIANO RCDATA \"${NOVA_PACK_ESC}\"\n")
    # .rc musi byt v samotnom DLL (KorgusNova je len staticka kniznica so spolocnym kodom)
    target_sources(KorgusNova_VST3 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/nova_res.rc")
endif()

target_compile_definitions(KorgusNova PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(KorgusNova PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(NOVA_TEST)
    juce_add_console_app(NovaTest PRODUCT_NAME "NovaTest")
    target_sources(NovaTest PRIVATE Test/test.cpp Source/Synth.cpp Source/PianoBank.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(NovaTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Korgus Nova" JucePlugin_IsSynth=1)
    target_link_libraries(NovaTest PRIVATE juce::juce_dsp juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats)
endif()

if(NOVA_HOSTTEST)
    juce_add_console_app(NovaHostTest PRODUCT_NAME "NovaHostTest")
    target_sources(NovaHostTest PRIVATE Test/host.cpp)
    target_compile_definitions(NovaHostTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
    target_link_libraries(NovaHostTest PRIVATE juce::juce_audio_processors)
endif()

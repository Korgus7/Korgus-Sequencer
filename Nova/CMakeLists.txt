cmake_minimum_required(VERSION 3.22)

# Korgus Inspiria (predtym Korgus Inspire, Korgus Nova) - syntezator (klavir, pady, slaciky, husle, pluck...)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(KORGUS_BUILD "0" CACHE STRING "Build number")
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
    PRODUCT_NAME "Korgus Inspiria"
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
    Source/Params.cpp
    Source/PresetStore.cpp
    Source/PianoBank.cpp
    Source/InstrBank.cpp
    Source/Lang.cpp)

if(WIN32)
    # klavirne vzorky priamo v subore pluginu (prostriedok RCDATA)
    set(NOVA_PACK "${CMAKE_CURRENT_SOURCE_DIR}/Resources/piano.knpk")
    file(TO_NATIVE_PATH "${NOVA_PACK}" NOVA_PACK_NATIVE)
    string(REPLACE "\\" "\\\\" NOVA_PACK_ESC "${NOVA_PACK_NATIVE}")
    # realisticke nastroje (InstrBank)
    set(NOVA_INSTR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/instr.knin")
    file(TO_NATIVE_PATH "${NOVA_INSTR}" NOVA_INSTR_NATIVE)
    string(REPLACE "\\" "\\\\" NOVA_INSTR_ESC "${NOVA_INSTR_NATIVE}")
    # povodny klavir (Salamander) na porovnanie
    set(NOVA_PACK2 "${CMAKE_CURRENT_SOURCE_DIR}/Resources/piano_old.knpk")
    file(TO_NATIVE_PATH "${NOVA_PACK2}" NOVA_PACK2_NATIVE)
    string(REPLACE "\\" "\\\\" NOVA_PACK2_ESC "${NOVA_PACK2_NATIVE}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/nova_res.rc" "PIANO RCDATA \"${NOVA_PACK_ESC}\"\nPIANO2 RCDATA \"${NOVA_PACK2_ESC}\"\nINSTR RCDATA \"${NOVA_INSTR_ESC}\"\n")
    # .rc musi byt v samotnom DLL (KorgusNova je len staticka kniznica so spolocnym kodom)
    target_sources(KorgusNova_VST3 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/nova_res.rc")
endif()

# zabudovane vzorky nastrojov (FLAC) - vyber "Vzorky" vo vrstve
file(GLOB NOVA_SAMPLES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/samples/*.flac")
juce_add_binary_data(NovaSamples HEADER_NAME NovaSamples.h NAMESPACE NovaSamples SOURCES ${NOVA_SAMPLES})
set_target_properties(NovaSamples PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(KorgusNova PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    INSPIRE_BUILD=${KORGUS_BUILD})

target_link_libraries(KorgusNova PRIVATE
    NovaSamples
    juce::juce_dsp
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)


# mapa adries pre rozbor padov (offset z crash.txt -> funkcia)
if(MSVC)
    target_link_options(KorgusNova_VST3 PRIVATE /MAP)
endif()

if(NOVA_TEST)
    juce_add_console_app(NovaTest PRODUCT_NAME "NovaTest")
    target_sources(NovaTest PRIVATE Test/test.cpp Source/Synth.cpp Source/PianoBank.cpp Source/InstrBank.cpp Source/Params.cpp Source/PresetStore.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Lang.cpp)
    target_compile_definitions(NovaTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Korgus Nova" JucePlugin_IsSynth=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(NovaTest PRIVATE NovaSamples juce::juce_dsp juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats)
endif()

if(NOVA_HOSTTEST)
    juce_add_console_app(NovaHostTest PRODUCT_NAME "NovaHostTest")
    target_sources(NovaHostTest PRIVATE Test/host.cpp)
    target_compile_definitions(NovaHostTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
    target_link_libraries(NovaHostTest PRIVATE juce::juce_audio_processors juce::juce_gui_basics)
endif()

cmake_minimum_required(VERSION 3.22)

# staticky runtime - .exe nepotrebuje Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# cislo verzie: 1.0.<cislo buildu v CI> (lokalne 1.0.0)
set(KORGUS_BUILD "0" CACHE STRING "Build number")
project(KorgusSequencer VERSION 1.0.${KORGUS_BUILD})

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(KorgusSequencer
    PRODUCT_NAME "Korgus Sequencer"
    COMPANY_NAME "Korgus"
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE)

juce_add_binary_data(KorgusData SOURCES Resources/ui/index.html)

target_sources(KorgusSequencer PRIVATE
    Source/Main.cpp
    Source/Engine.cpp
    Source/SongParse.cpp
    Source/AudioStore.cpp
    Source/Melody.cpp
    Source/Separator.cpp
    Source/BuiltinFx.cpp
    Source/Autotune.cpp
    Source/Denoise.cpp
    Source/DrumKits.cpp
    Source/VideoExport.cpp
    Source/AudioAnalysis.cpp
    Source/Mp3Export.cpp
    Source/UiCommon.cpp
    Source/MainView.cpp
    Source/CrashGuard.cpp)

target_compile_definitions(KorgusSequencer PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:KorgusSequencer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:KorgusSequencer,JUCE_VERSION>")

# ASIO (len Windows) - cesta k ASIO SDK sa zadava pri preklade: -DASIO_SDK_DIR=...
if(WIN32 AND ASIO_SDK_DIR)
    target_compile_definitions(KorgusSequencer PRIVATE JUCE_ASIO=1)
    target_include_directories(KorgusSequencer PRIVATE "${ASIO_SDK_DIR}/common")
    message(STATUS "ASIO zapnute: ${ASIO_SDK_DIR}")
endif()

# export videa (Media Foundation) - len Windows
if(WIN32)
    target_sources(KorgusSequencer PRIVATE Source/VideoExportWin.cpp)
    target_link_libraries(KorgusSequencer PRIVATE mfplat mfreadwrite mfuuid ole32)
endif()

file(GLOB SHINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/shine/*.c")
add_library(shine STATIC ${SHINE_SOURCES})
set_target_properties(shine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(shine PRIVATE /wd4244 /wd4267 /wd4305)
    set_property(TARGET shine PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(KorgusSequencer
    PRIVATE
        shine
        KorgusData
        juce::juce_dsp
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

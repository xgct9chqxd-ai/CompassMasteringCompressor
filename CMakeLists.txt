cmake_minimum_required(VERSION 3.18)
project(CompassMasteringLimiter VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

add_subdirectory(reference_core EXCLUDE_FROM_ALL)
add_subdirectory(reference_harness EXCLUDE_FROM_ALL)
add_subdirectory(reference_tests EXCLUDE_FROM_ALL)

juce_add_plugin(CompassMasteringLimiter
    COMPANY_NAME "Compass"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Comp
    PLUGIN_CODE CML1
    FORMATS VST3
    PRODUCT_NAME "Compass Mastering Limiter"
)

add_custom_command(TARGET CompassMasteringLimiter_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/Library/Audio/Plug-Ins/VST3"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Compass Mastering Limiter.vst3"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_CURRENT_BINARY_DIR}/CompassMasteringLimiter_artefacts/VST3/Compass Mastering Limiter.vst3"
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Compass Mastering Limiter.vst3"
    VERBATIM
)

target_sources(CompassMasteringLimiter PRIVATE
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginProcessor.h
    Source/Plugin/PluginEditor.cpp
    Source/Plugin/PluginEditor.h
)

target_compile_definitions(CompassMasteringLimiter PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(CompassMasteringLimiter PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
)

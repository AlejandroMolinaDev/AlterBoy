cmake_minimum_required(VERSION 3.25)

# Universal binary on macOS unless the caller overrides it. Must be set before project().
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

project(Metamorfo VERSION 0.1.0 LANGUAGES C CXX)

option(METAMORFO_BUILD_TESTS "Build unit tests" ON)

include(cmake/CompilerSettings.cmake)
include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# metamorfo_dsp: JUCE-free real-time DSP building blocks (strict warnings).
# ---------------------------------------------------------------------------
add_library(metamorfo_dsp STATIC
  src/dsp/MidiPitchControl.cpp
  src/dsp/PitchDetector.cpp
  src/dsp/PsolaEngine.cpp)
target_include_directories(metamorfo_dsp PUBLIC src)
target_link_libraries(metamorfo_dsp PRIVATE metamorfo_warnings)

# Embedded resources (OFL fonts, see THIRD_PARTY_LICENSES.md).
juce_add_binary_data(metamorfo_resources
  NAMESPACE MetamorfoBinary
  HEADER_NAME MetamorfoBinary.h
  SOURCES
    resources/fonts/BarlowCondensed-SemiBold.ttf
    resources/fonts/BarlowCondensed-Medium.ttf
    resources/fonts/ShareTechMono-Regular.ttf)

# ---------------------------------------------------------------------------
# metamorfo_core: plugin sources + JUCE modules. INTERFACE so that both the
# plugin and the test executable compile their own copy of JUCE.
# ---------------------------------------------------------------------------
add_library(metamorfo_core INTERFACE)
target_sources(metamorfo_core INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/Parameters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/PresetManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PluginEditor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/MetamorfoLookAndFeel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/Knob.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/ToggleLever3.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/PresetBar.cpp)
target_include_directories(metamorfo_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(metamorfo_core INTERFACE
  METAMORFO_VERSION_STRING="${PROJECT_VERSION}"
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_REPORT_APP_USAGE=0
  JUCE_MODAL_LOOPS_PERMITTED=0)
# Sources contain UTF-8 text (Spanish UI strings).
target_compile_options(metamorfo_core INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(metamorfo_core INTERFACE
  metamorfo_dsp
  metamorfo_resources
  clap_juce_extensions
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_dsp
  juce::juce_gui_basics)

# ---------------------------------------------------------------------------
# Plugin. IDs below are part of the product identity: NEVER change them,
# hosts would treat the plugin as a different one and saved projects break.
# ---------------------------------------------------------------------------
juce_add_plugin(Metamorfo
  COMPANY_NAME "Metamorfo Audio"
  COMPANY_WEBSITE ""
  BUNDLE_ID com.metamorfoaudio.metamorfo
  PLUGIN_MANUFACTURER_CODE Mtmf
  PLUGIN_CODE Mmf1
  PRODUCT_NAME "Metamorfo"
  PLUGIN_NAME "Metamorfo"
  DESCRIPTION "Monophonic vocal pitch and formant manipulator"
  FORMATS VST3 AU Standalone
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  VST3_CATEGORIES Fx "Pitch Shift"
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_compile_definitions(Metamorfo PUBLIC
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Metamorfo
  PRIVATE
    metamorfo_core
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET Metamorfo
  CLAP_ID "com.metamorfoaudio.metamorfo"
  CLAP_FEATURES audio-effect pitch-shifter mono stereo)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(METAMORFO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

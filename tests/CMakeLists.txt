# Unit tests. The executable compiles its own copy of the plugin core + JUCE,
# so it runs without any plugin host.

add_executable(MetamorfoTests
  unit/TestMain.cpp
  unit/MidiPitchControlTests.cpp
  unit/ProcessorTests.cpp
  unit/StateTests.cpp
  unit/HostBehaviourTests.cpp
  unit/LatencyTests.cpp
  unit/PitchDetectorTests.cpp
  unit/EngineTests.cpp)

target_compile_definitions(MetamorfoTests PRIVATE
  JUCE_STANDALONE_APPLICATION=1)

# Tests compare floats bit-exactly on purpose (passthrough, null tests).
target_compile_options(MetamorfoTests PRIVATE
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-float-equal>)

target_link_libraries(MetamorfoTests PRIVATE
  metamorfo_core
  Catch2::Catch2
  juce::juce_recommended_config_flags)

include("${Catch2_SOURCE_DIR}/extras/Catch.cmake")
catch_discover_tests(MetamorfoTests DISCOVERY_MODE PRE_TEST)

# Offline render host (SPEC §7 "harness de render"): WAV in -> plugin -> WAV out + JSON report.
add_executable(MetamorfoRender render/RenderMain.cpp)
target_compile_definitions(MetamorfoRender PRIVATE JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(MetamorfoRender PRIVATE
  metamorfo_core
  juce::juce_audio_formats
  juce::juce_recommended_config_flags)

# Host probe: loads the BUILT VST3 / CLAP binaries like a DAW does and checks the
# host-facing contract (effect, MIDI input / CLAP note port, layouts, processing, state).
add_executable(MetamorfoHostProbe host/HostProbeMain.cpp host/ClapProbe.cpp)
target_compile_definitions(MetamorfoHostProbe PRIVATE
  JUCE_STANDALONE_APPLICATION=1
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(MetamorfoHostProbe PRIVATE
  clap
  juce::juce_audio_processors
  juce::juce_events
  juce::juce_recommended_config_flags)

# The probes need the plugin binaries: `cmake --build` builds every target by default.
add_test(NAME host_probe_vst3
  COMMAND MetamorfoHostProbe vst3 "$<GENEX_EVAL:$<TARGET_PROPERTY:Metamorfo_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
if(APPLE)
  set(metamorfo_clap_path "$<TARGET_BUNDLE_DIR:Metamorfo_CLAP>")
else()
  set(metamorfo_clap_path "$<TARGET_FILE:Metamorfo_CLAP>")
endif()
add_test(NAME host_probe_clap COMMAND MetamorfoHostProbe clap "${metamorfo_clap_path}")

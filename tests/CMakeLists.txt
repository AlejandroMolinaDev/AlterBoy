# Unit tests. The executable compiles its own copy of the plugin core + JUCE,
# so it runs without any plugin host.

add_executable(MetamorfoTests
  unit/TestMain.cpp
  unit/MidiPitchControlTests.cpp
  unit/ProcessorTests.cpp
  unit/StateTests.cpp)

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

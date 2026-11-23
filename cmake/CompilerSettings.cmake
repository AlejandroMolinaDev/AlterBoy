# Project-wide compiler settings. Included before any target is created.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static MSVC runtime: users do not need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Needed so the plugin shared-code static library can be linked into the
# CLAP/VST3 module and into the test executable.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
  endif()
endif()

# Strict warnings for the JUCE-free DSP library. Targets that compile JUCE
# module sources use juce::juce_recommended_warning_flags instead.
add_library(metamorfo_warnings INTERFACE)
if(MSVC)
  target_compile_options(metamorfo_warnings INTERFACE /W4 /permissive- /utf-8)
else()
  target_compile_options(metamorfo_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wconversion)
endif()

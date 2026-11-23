# All third-party dependencies, pinned. Update versions ONLY here and record the
# change in THIRD_PARTY_LICENSES.md and docs/ROADMAP.md.

include("${CMAKE_CURRENT_LIST_DIR}/GetCPM.cmake")

# JUCE 9.0.6 (tag commit 654bdbc7a9db0dae90849b44abe91f339e9f0768). AGPLv3 / commercial.
CPMAddPackage(
  NAME JUCE
  GITHUB_REPOSITORY juce-framework/JUCE
  GIT_TAG 9.0.6
  GIT_SHALLOW TRUE
  OPTIONS "JUCE_BUILD_EXAMPLES OFF" "JUCE_BUILD_EXTRAS OFF")

# Unofficial CLAP support for JUCE (MIT). Pulls clap + clap-helpers as submodules.
# Commit: "Upgrade clap to 1.2.10 and clap-helpers to latest (#197)".
CPMAddPackage(
  NAME clap-juce-extensions
  GITHUB_REPOSITORY free-audio/clap-juce-extensions
  GIT_TAG 3837f4a6ed2ab0ba46aba69ae5b7041c7c64ce74
  EXCLUDE_FROM_ALL YES)

if(METAMORFO_BUILD_TESTS)
  # Catch2 v3 (BSL-1.0).
  CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.9.1
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL YES)
endif()

cmake_minimum_required(VERSION 3.22)

# Must come before project(): set later, it was ignored and the builds required the build machine's
# macOS (27.0), so they would not load on older Macs (found 2026-10-23). arm64 slices start at 11.0 anyway.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS")

project(PowerDrum VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary" FORCE)
endif()

option(POWERDRUM_COPY_AFTER_BUILD "Install plug-ins into the user plug-in folders after build" OFF)

# cmake/patch_juce.cmake fixes JUCE before it is used (Windows: closing the editor could hang for ever).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
  set(JUCE_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE")
  include(cmake/patch_juce.cmake)
  add_subdirectory(third_party/JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                            GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850
                            PATCH_COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_juce.cmake")
  FetchContent_MakeAvailable(JUCE)
endif()

# Cross-compiling for Windows: the manifest helper needs an emulator (Wine) to run here.
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  set(POWER_VST3_AUTO_MANIFEST FALSE)
else()
  set(POWER_VST3_AUTO_MANIFEST TRUE)
endif()

set(POWERDRUM_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND POWERDRUM_FORMATS AU)
endif()

juce_add_plugin(PowerDrum
  COMPANY_NAME "MyDarknessDays"
  COMPANY_WEBSITE ""
  BUNDLE_ID "com.mydarknessdays.powerdrum"
  PLUGIN_MANUFACTURER_CODE Mddy
  PLUGIN_CODE Pwdr
  PRODUCT_NAME "PowerDrum"
  FORMATS ${POWERDRUM_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Instrument Drum
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_AUTO_MANIFEST ${POWER_VST3_AUTO_MANIFEST}
  COPY_PLUGIN_AFTER_BUILD ${POWERDRUM_COPY_AFTER_BUILD})

set(POWERDRUM_CORE_SOURCES
  Source/Params.cpp
  Source/Presets.cpp
)

target_sources(PowerDrum PRIVATE
  ${POWERDRUM_CORE_SOURCES}
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_include_directories(PowerDrum PRIVATE Source)
target_compile_definitions(PowerDrum PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(PowerDrum
  PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Headless DSP/preset tests: the same engine code, no plug-in wrapper.
juce_add_console_app(powerdrum_tests PRODUCT_NAME "PowerDrum tests")
target_sources(powerdrum_tests PRIVATE ${POWERDRUM_CORE_SOURCES} Tests/PowerDrumTests.cpp)
target_include_directories(powerdrum_tests PRIVATE Source)
target_compile_definitions(powerdrum_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(powerdrum_tests
  PRIVATE juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME powerdrum_tests COMMAND powerdrum_tests)

# Wrapper tests: processor, state, presets through the host path and the editor, linked
# against the plug-in's shared code. (Shared code exports JUCE_STANDALONE_APPLICATION=0.)
juce_add_console_app(powerdrum_wrapper_tests PRODUCT_NAME "PowerDrum wrapper tests")
get_target_property(POWERDRUM_WRAP_DEFS powerdrum_wrapper_tests COMPILE_DEFINITIONS)
list(REMOVE_ITEM POWERDRUM_WRAP_DEFS "JUCE_STANDALONE_APPLICATION=1")
set_target_properties(powerdrum_wrapper_tests PROPERTIES COMPILE_DEFINITIONS "${POWERDRUM_WRAP_DEFS}")
target_sources(powerdrum_wrapper_tests PRIVATE Tests/WrapperTests.cpp)
target_include_directories(powerdrum_wrapper_tests PRIVATE Source)
target_link_libraries(powerdrum_wrapper_tests PRIVATE PowerDrum juce::juce_audio_utils)
add_test(NAME powerdrum_wrapper_tests COMMAND powerdrum_wrapper_tests)

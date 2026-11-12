# host_check: loads the built PowerDrum (AU or VST3) as a DAW does and checks its 17 outputs and routing.
#   cmake -S Tools/host_check -B <dir> -DJUCE_DIR=<third_party/JUCE>; <dir>/host_check_artefacts/Release/host_check <plug-in>
cmake_minimum_required(VERSION 3.22)
project(PowerDrumHostCheck VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
  set(HOST_AU 1)
else()
  set(HOST_AU 0)   # Windows (cross-built, run under Wine): VST3 only
endif()
set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../third_party/JUCE" CACHE PATH "JUCE source")
add_subdirectory("${JUCE_DIR}" JUCE)
juce_add_console_app(host_check PRODUCT_NAME "host_check")
target_sources(host_check PRIVATE main.cpp)
target_compile_definitions(host_check PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=${HOST_AU} JUCE_PLUGINHOST_LADSPA=0
                           JUCE_PLUGINHOST_LV2=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(host_check PRIVATE juce::juce_audio_processors juce::juce_gui_basics
                      PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(APPLE)
  target_link_libraries(host_check PRIVATE "-framework CoreAudioKit")   # AUGenericView (hosting AU)
endif()

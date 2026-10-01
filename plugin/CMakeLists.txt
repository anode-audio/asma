# SPDX-License-Identifier: GPL-3.0-only
# asma_ui holds the app's own JUCE code. It is an INTERFACE library so the
# plugin and the plugin tests each compile it, and JUCE, once.
add_library(asma_ui INTERFACE)
file(GLOB ASMA_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
target_sources(asma_ui INTERFACE ${ASMA_UI_SOURCES})
target_include_directories(asma_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_ui INTERFACE
  asma::audio
  Ableton::Link
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_recommended_config_flags)
target_compile_definitions(asma_ui INTERFACE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_REPORT_APP_USAGE=0)

set(ASMA_FORMATS Standalone VST3)
if(APPLE)
  list(APPEND ASMA_FORMATS AU)
elseif(UNIX)
  list(APPEND ASMA_FORMATS LV2)
endif()

juce_add_plugin(asma_plugin
  COMPANY_NAME "Anode Labs"
  COMPANY_WEBSITE "https://anode-labs.com"
  COMPANY_EMAIL "support@anode-labs.com"
  PLUGIN_MANUFACTURER_CODE Anod
  PLUGIN_CODE Asma
  PRODUCT_NAME "asma"
  VERSION ${PROJECT_VERSION}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  VST3_CATEGORIES Instrument Sampler
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  LV2URI "https://anode-labs.com/asma"
  BUNDLE_ID com.anodelabs.asma
  FORMATS ${ASMA_FORMATS})
# JUCE's warning set for the plugin build; the tests keep Catch2's exact
# float comparisons, which -Wfloat-equal would flag.
target_link_libraries(asma_plugin PRIVATE asma_ui juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET asma_plugin
  CLAP_ID "com.anodelabs.asma"
  CLAP_FEATURES instrument sampler)

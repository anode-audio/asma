# SPDX-License-Identifier: GPL-3.0-only
# asma_ui holds the app's own JUCE code. It is an INTERFACE library so the
# plugin and the plugin tests each compile it, and JUCE, once.
add_library(asma_ui INTERFACE)
file(GLOB ASMA_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp)
target_sources(asma_ui INTERFACE ${ASMA_UI_SOURCES})
target_include_directories(asma_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The Anode fonts (SIL OFL, licences beside them), compiled in.
juce_add_binary_data(asma_fonts NAMESPACE AsmaFonts HEADER_NAME AsmaFonts.h SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf
  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Medium.ttf
  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-SemiBold.ttf
  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/JetBrainsMono-Regular.ttf
  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/SpaceGrotesk-SemiBold.ttf)
set_target_properties(asma_fonts PROPERTIES POSITION_INDEPENDENT_CODE ON) # the LV2 plugin is a shared library

target_link_libraries(asma_ui INTERFACE
  asma_fonts
  asma::audio
  Ableton::Link
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_recommended_config_flags)
# The UI's text is UTF-8 in the source (arrows, middle dots): MSVC must read
# it so and keep it so, whatever the system code page.
target_compile_options(asma_ui INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
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

# The standalone runs asma-scan from beside its own executable.
add_dependencies(asma_plugin_Standalone asma-scan)
add_custom_command(TARGET asma_plugin_Standalone POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:asma-scan> $<TARGET_FILE_DIR:asma_plugin_Standalone>)
# Every format writes the library through the asma helper beside its binary
# (the standalone only for a retry). It goes in as asma-cli: the app's and
# the bundles' own binaries are called asma.
foreach(format Standalone VST3 AU LV2 CLAP)
  if(TARGET asma_plugin_${format})
    add_dependencies(asma_plugin_${format} asma)
    add_custom_command(TARGET asma_plugin_${format} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:asma>
              $<TARGET_FILE_DIR:asma_plugin_${format}>/asma-cli${CMAKE_EXECUTABLE_SUFFIX})
  endif()
endforeach()

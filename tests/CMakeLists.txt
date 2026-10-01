# SPDX-License-Identifier: GPL-3.0-only
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.16.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(asma_test_child child/asma_test_child.cpp)
target_link_libraries(asma_test_child PRIVATE asma_cli_support)
asma_set_warnings(asma_test_child)

file(GLOB ASMA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(asma_tests ${ASMA_TEST_SOURCES})
target_link_libraries(asma_tests PRIVATE asma_cli_support asma::audio Catch2::Catch2WithMain)
target_compile_definitions(asma_tests PRIVATE
  ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  ASMA_VERSION="${PROJECT_VERSION}"
  ASMA_CLI_PATH="$<TARGET_FILE:asma>"
  ASMA_SCAN_PATH="$<TARGET_FILE:asma-scan>"
  ASMA_TEST_CHILD_PATH="$<TARGET_FILE:asma_test_child>")
add_dependencies(asma_tests asma asma-scan asma_test_child)
asma_set_warnings(asma_tests)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(asma_tests)

if(ASMA_BUILD_PLUGIN)
  # The JUCE side: the processor and the UI, headless.
  juce_add_console_app(asma_plugin_tests PRODUCT_NAME "asma plugin tests")
  file(GLOB ASMA_PLUGIN_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/plugin/*.cpp)
  target_sources(asma_plugin_tests PRIVATE ${ASMA_PLUGIN_TEST_SOURCES})
  target_include_directories(asma_plugin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(asma_plugin_tests PRIVATE asma_ui Catch2::Catch2WithMain)
  target_compile_definitions(asma_plugin_tests PRIVATE
    ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    ASMA_CLI_PATH="$<TARGET_FILE:asma>"
    ASMA_SCAN_PATH="$<TARGET_FILE:asma-scan>"
    JUCE_MODAL_LOOPS_PERMITTED=1) # lets a test run the message loop
  add_dependencies(asma_plugin_tests asma asma-scan)
  catch_discover_tests(asma_plugin_tests)
endif()

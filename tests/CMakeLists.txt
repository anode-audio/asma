# SPDX-License-Identifier: GPL-3.0-only
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.16.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

file(GLOB ASMA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(asma_tests ${ASMA_TEST_SOURCES})
target_link_libraries(asma_tests PRIVATE asma::core Catch2::Catch2WithMain)
target_compile_definitions(asma_tests PRIVATE
  ASMA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
asma_set_warnings(asma_tests)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(asma_tests)

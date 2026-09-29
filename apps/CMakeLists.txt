# SPDX-License-Identifier: GPL-3.0-only
add_library(asma_cli_support STATIC Args.cpp CliCommon.cpp SearchArgs.cpp)
target_include_directories(asma_cli_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asma_cli_support PUBLIC asma::core)
if(WIN32)
  target_link_libraries(asma_cli_support PUBLIC shell32)
endif()
asma_set_warnings(asma_cli_support)

add_executable(asma asma_main.cpp OrganiseCommands.cpp)
target_link_libraries(asma PRIVATE asma_cli_support asma::audio)
target_compile_definitions(asma PRIVATE ASMA_VERSION="${PROJECT_VERSION}")
asma_set_warnings(asma)

add_executable(asma-scan asma_scan_main.cpp)
target_link_libraries(asma-scan PRIVATE asma_cli_support)
asma_set_warnings(asma-scan)

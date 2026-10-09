# SPDX-License-Identifier: GPL-3.0-only
file(GLOB ASMA_CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

set(ASMA_TOKENS_FILE ${PROJECT_SOURCE_DIR}/data/instrument_tokens.txt)
file(READ ${ASMA_TOKENS_FILE} ASMA_INSTRUMENT_TOKENS)
configure_file(src/InstrumentTokensData.cpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/InstrumentTokensData.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ASMA_TOKENS_FILE})
list(APPEND ASMA_CORE_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/InstrumentTokensData.cpp)

# The trash speaks each system's own API: Foundation on macOS, the shell on
# Windows (the freedesktop.org trash is portable C++).
if(APPLE)
  list(APPEND ASMA_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/Trash_mac.mm)
endif()

add_library(asma_core STATIC ${ASMA_CORE_SOURCES})
add_library(asma::core ALIAS asma_core)
target_include_directories(asma_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_core
  PUBLIC asma_sqlite
  PRIVATE asma_xxhash asma_dr_libs asma_stb asma_ebur128 efsw-static Threads::Threads)
if(APPLE)
  target_link_libraries(asma_core PRIVATE "-framework Foundation")
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/Trash_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
if(WIN32)
  target_link_libraries(asma_core PRIVATE ole32 shell32)
  target_compile_definitions(asma_core PUBLIC
    NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
asma_set_warnings(asma_core)
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor_dr.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor_stb_vorbis.c
  PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")

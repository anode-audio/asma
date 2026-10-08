# SPDX-License-Identifier: GPL-3.0-only
include(FetchContent)

# SOURCE_SUBDIR points at a directory that does not exist so FetchContent only
# downloads; we define our own targets below.
FetchContent_Declare(sqlite
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
  URL_HASH SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
  SOURCE_SUBDIR _none)
FetchContent_Declare(xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG v0.8.4
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none)
FetchContent_Declare(dr_libs
  GIT_REPOSITORY https://github.com/mackron/dr_libs.git
  GIT_TAG dfe8377631000664666519fdb83da193fd8037f4
  SOURCE_SUBDIR _none)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
  SOURCE_SUBDIR _none)
FetchContent_Declare(ebur128
  GIT_REPOSITORY https://github.com/jiixyj/libebur128.git
  GIT_TAG v1.2.6
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none)
# Its own CMakeLists fetches signalsmith-linear 0.6.4.
FetchContent_Declare(signalsmith_stretch
  GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
  GIT_TAG a670068d9aeb64913331d5cc29337b19a457a7df # 1.4.0
  GIT_SHALLOW FALSE)
# Folder change notices (FSEvents, ReadDirectoryChangesW, inotify); MIT.
FetchContent_Declare(efsw
  GIT_REPOSITORY https://github.com/SpartanJ/efsw.git
  GIT_TAG 41ddf6822f2d0dec7e14fafa09c4cef391137b20 # 1.7.2
  GIT_SHALLOW FALSE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_TEST_APP OFF CACHE BOOL "" FORCE)
set(EFSW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sqlite xxhash dr_libs stb ebur128 signalsmith_stretch efsw)
# Header-only; SYSTEM keeps their warnings out of ours.
set_target_properties(signalsmith-stretch signalsmith-linear PROPERTIES SYSTEM TRUE)

# The LV2 plugin is a shared library: everything it links must be PIC.
set_target_properties(efsw-static PROPERTIES POSITION_INDEPENDENT_CODE ON SYSTEM TRUE)
if(MSVC)
  target_compile_options(efsw-static PRIVATE /w)
else()
  target_compile_options(efsw-static PRIVATE -w)
endif()

add_library(asma_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(asma_sqlite SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(asma_sqlite PRIVATE
  SQLITE_ENABLE_FTS5
  SQLITE_THREADSAFE=1
  SQLITE_DQS=0
  SQLITE_OMIT_LOAD_EXTENSION)
target_link_libraries(asma_sqlite PUBLIC Threads::Threads)
if(UNIX AND NOT APPLE)
  target_link_libraries(asma_sqlite PUBLIC m)
endif()

add_library(asma_xxhash STATIC ${xxhash_SOURCE_DIR}/xxhash.c)
target_include_directories(asma_xxhash SYSTEM PUBLIC ${xxhash_SOURCE_DIR})

add_library(asma_dr_libs INTERFACE)
target_include_directories(asma_dr_libs SYSTEM INTERFACE ${dr_libs_SOURCE_DIR})

add_library(asma_stb INTERFACE)
target_include_directories(asma_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

add_library(asma_ebur128 STATIC ${ebur128_SOURCE_DIR}/ebur128/ebur128.c)
target_include_directories(asma_ebur128
  SYSTEM PUBLIC ${ebur128_SOURCE_DIR}/ebur128
  PRIVATE ${ebur128_SOURCE_DIR}/ebur128/queue)
if(MSVC)
  target_compile_definitions(asma_ebur128 PRIVATE _USE_MATH_DEFINES)
  target_compile_options(asma_ebur128 PRIVATE /w)
else()
  target_compile_options(asma_ebur128 PRIVATE -w)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(asma_ebur128 PUBLIC m)
endif()

if(ASMA_BUILD_PLUGIN)
  # AGPLv3, which suits asma's GPLv3.
  FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG be29c81492b6151c8ea8d14c840e1311963b3a83 # 9.0.3
    GIT_SHALLOW FALSE)
  # CLAP for JUCE plugins; clones CLAP itself as submodules.
  FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e
    GIT_SHALLOW FALSE)
  # Tempo sync for the standalone; GPLv2 or later.
  FetchContent_Declare(ableton_link
    GIT_REPOSITORY https://github.com/Ableton/link.git
    GIT_TAG 9c9091275e707ab09d09a5a608fcdb84bf0dec85 # Link-4.1
    GIT_SHALLOW FALSE
    SOURCE_SUBDIR _none)
  FetchContent_MakeAvailable(juce clap_juce_extensions ableton_link)
  # Link's config sets CMAKE_CXX_STANDARD to 17; a function keeps that out of
  # our scope, while the Ableton::Link target it defines stays global.
  function(asma_add_ableton_link)
    include(${ableton_link_SOURCE_DIR}/AbletonLinkConfig.cmake)
  endfunction()
  asma_add_ableton_link()
endif()

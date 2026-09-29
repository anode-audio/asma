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
FetchContent_MakeAvailable(sqlite xxhash dr_libs stb ebur128 signalsmith_stretch)
# Header-only; SYSTEM keeps their warnings out of ours.
set_target_properties(signalsmith-stretch signalsmith-linear PROPERTIES SYSTEM TRUE)

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

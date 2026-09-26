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
FetchContent_MakeAvailable(sqlite xxhash dr_libs stb)

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

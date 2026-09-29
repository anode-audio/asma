# SPDX-License-Identifier: GPL-3.0-only
file(GLOB ASMA_AUDIO_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(asma_audio STATIC ${ASMA_AUDIO_SOURCES})
add_library(asma::audio ALIAS asma_audio)
target_include_directories(asma_audio
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(asma_audio PUBLIC asma::core Threads::Threads)
asma_set_warnings(asma_audio)

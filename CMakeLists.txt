# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(asma VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ASMA_BUILD_TESTS "Build the test suite" ON)

find_package(Threads REQUIRED)

include(cmake/Warnings.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(core)
add_subdirectory(apps)

if(ASMA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

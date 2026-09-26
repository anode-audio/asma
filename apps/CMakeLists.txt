# SPDX-License-Identifier: GPL-3.0-only
add_executable(asma asma_main.cpp)
target_link_libraries(asma PRIVATE asma::core)
target_compile_definitions(asma PRIVATE ASMA_VERSION="${PROJECT_VERSION}")
asma_set_warnings(asma)

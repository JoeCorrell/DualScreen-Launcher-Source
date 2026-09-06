cmake_minimum_required(VERSION 3.22)
project(plza_module LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(EDEN "${CMAKE_CURRENT_LIST_DIR}/../../../eden-upstream")
add_library(plza_module SHARED module.cpp hooks.cpp assets.cpp
    "${EDEN}/src/android/app/src/main/jni/plza_companion.cpp")
target_include_directories(plza_module PRIVATE . "${EDEN}/src"
    "${EDEN}/src/android/app/src/main" "${EDEN}/.cache/cpm/fmt/12.1.0/include")
target_compile_definitions(plza_module PRIVATE PLZA_EXTERNAL_MODULE
    PLZA_PACKAGE_VERSION="${PLZA_PACKAGE_VERSION}")
target_compile_options(plza_module PRIVATE -Wall -Wextra -fvisibility=hidden)
target_link_options(plza_module PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384
    "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/exports.map")

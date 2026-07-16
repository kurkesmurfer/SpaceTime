cmake_minimum_required(VERSION 3.22)

if(NOT "${METAMODULE_SDK_DIR}" STREQUAL "")
    message("METAMODULE_SDK_DIR set by CMake variable ${METAMODULE_SDK_DIR}")
elseif(DEFINED ENV{METAMODULE_SDK_DIR})
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
else()
    set(METAMODULE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../metamodule-plugin-sdk")
endif()

if(NOT DEFINED INSTALL_DIR)
    set(INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins)
endif()

include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(SpaceTimeBusProbe
        VERSION 0.1
        DESCRIPTION "SpaceTime shared-memory bus probe"
        LANGUAGES C CXX ASM)

add_library(SpaceTimeProbe STATIC)
target_sources(SpaceTimeProbe PRIVATE
        src/Probe.cpp
        src/plugin.cpp)
target_include_directories(SpaceTimeProbe PRIVATE
        src
        ../dsp)
set_property(TARGET SpaceTimeProbe PROPERTY CXX_STANDARD 20)

create_plugin(
        SOURCE_LIB SpaceTimeProbe
        PLUGIN_NAME SpaceTimeProbe
        PLUGIN_JSON ${CMAKE_CURRENT_LIST_DIR}/plugin.json
        SOURCE_ASSETS ${CMAKE_CURRENT_LIST_DIR}/assets
        DESTINATION ${INSTALL_DIR})

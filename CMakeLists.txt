cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{TAB5_PROJECT_VER} AND NOT "$ENV{TAB5_PROJECT_VER}" STREQUAL "")
    set(PROJECT_VER "$ENV{TAB5_PROJECT_VER}")
endif()

set(TAB5_DEFAULT_ADF_PATH "C:/Espressif/tools/esp-adf")
if(NOT DEFINED ENV{ADF_PATH} AND EXISTS "${TAB5_DEFAULT_ADF_PATH}/CMakeLists.txt")
    set(ENV{ADF_PATH} "${TAB5_DEFAULT_ADF_PATH}")
endif()

if(DEFINED ENV{ADF_PATH} AND EXISTS "$ENV{ADF_PATH}/components/audio_pipeline/CMakeLists.txt")
    list(APPEND EXTRA_COMPONENT_DIRS
        "$ENV{ADF_PATH}/components/audio_sal"
        "$ENV{ADF_PATH}/components/audio_pipeline"
        "$ENV{ADF_PATH}/components/esp-adf-libs")
    set(TAB5_ADF_AVAILABLE 1 CACHE BOOL "ESP-ADF components are available")
    message(STATUS "ESP-ADF enabled from $ENV{ADF_PATH}")
else()
    set(TAB5_ADF_AVAILABLE 0 CACHE BOOL "ESP-ADF components are available")
    message(STATUS "ESP-ADF not found; ADF audio support will build as a stub")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(M5stack_tab5_ssh_client)

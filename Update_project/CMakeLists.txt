cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{TAB5_UPDATER_VER} AND NOT "$ENV{TAB5_UPDATER_VER}" STREQUAL "")
    set(PROJECT_VER "$ENV{TAB5_UPDATER_VER}")
elseif(DEFINED ENV{TAB5_PROJECT_VER} AND NOT "$ENV{TAB5_PROJECT_VER}" STREQUAL "")
    set(PROJECT_VER "$ENV{TAB5_PROJECT_VER}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(tab5_factory_updater)

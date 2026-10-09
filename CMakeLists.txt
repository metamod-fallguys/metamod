cmake_minimum_required(VERSION 3.21)
option(FORMAT_VALIDATION_ONLY "Configure formatting without compilers or native dependencies" OFF)
if(FORMAT_VALIDATION_ONLY)
    project(FormatValidation LANGUAGES NONE)
    include(cmake/FormatValidation.cmake)
    return()
endif()
include(cmake/Platform.cmake)
project(metamod LANGUAGES C CXX)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
include(cmake/SDK.cmake)

add_library(metamod SHARED
    src/api_hook.cpp
    src/api_info.cpp
    src/CDetour/asm/asm.cpp
    src/CDetour/detours.cpp
    src/commands_meta.cpp
    src/conf_meta.cpp
    src/dllapi.cpp
    src/engineinfo.cpp
    src/engine_api.cpp
    src/game_autodetect.cpp
    src/game_support.cpp
    src/h_export.cpp
    src/linkgame.cpp
    src/linkplug.cpp
    src/log_meta.cpp
    src/metamod.cpp
    src/meta_eiface.cpp
    src/mhook.cpp
    src/mlist.cpp
    src/mplayer.cpp
    src/mplugin.cpp
    src/mqueue.cpp
    src/mreg.cpp
    src/mutil.cpp
    src/osdep.cpp
    src/osdep_p.cpp
    src/reg_support.cpp
    src/sdk_util.cpp
    src/studioapi.cpp
    src/support_meta.cpp
    src/thread_logparse.cpp
    src/vdate.cpp
)
mmfg_configure_target(metamod)
target_link_libraries(metamod PRIVATE Metamod::SDK)
target_compile_definitions(metamod PRIVATE __METAMOD_BUILD__)
target_include_directories(metamod PRIVATE src/CDetour)
mmfg_capstone_library()
target_link_libraries(metamod PRIVATE capstone-static Capstone::Headers)
if(WIN32)
    target_sources(metamod PRIVATE src/osdep_detect_gamedll_win32.cpp src/osdep_linkent_win32.cpp src/res_meta.rc)
    target_link_options(metamod PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/metamod.def")
    target_compile_definitions(metamod PRIVATE __INTERNALS_USE_REGPARAMS__)
else()
    target_sources(metamod PRIVATE src/osdep_detect_gamedll_linux.cpp src/osdep_linkent_linux.cpp)
    target_compile_definitions(metamod PRIVATE $<$<NOT:$<CONFIG:Debug>>:__INTERNALS_USE_REGPARAMS__>)
    mmfg_procmap_library()
    target_link_libraries(metamod PRIVATE procmap)
endif()

include(cmake/FormatValidation.cmake)

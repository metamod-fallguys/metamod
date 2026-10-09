include_guard(GLOBAL)
get_filename_component(MMFG_METAMOD_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Build.cmake")

if(NOT TARGET Metamod::SDK)
    add_library(mmfg_metamod_sdk INTERFACE)
    add_library(Metamod::SDK ALIAS mmfg_metamod_sdk)
    target_include_directories(mmfg_metamod_sdk INTERFACE
        "${MMFG_METAMOD_ROOT}/include"
        "${MMFG_METAMOD_ROOT}/include/HLSDK/common"
        "${MMFG_METAMOD_ROOT}/include/HLSDK/dlls"
        "${MMFG_METAMOD_ROOT}/include/HLSDK/engine"
        "${MMFG_METAMOD_ROOT}/include/HLSDK/pm_shared")
endif()

# metamod component test suite.
#
# Enabled from the component root with -DMMFG_METAMOD_BUILD_TESTS=ON. The tests
# compile the shipped translation units against the stubs and fixtures in this
# directory, so what runs is the production code, not a copy of it.

get_filename_component(MMFG_TEST_COMPONENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(MMFG_TEST_INCLUDE_DIRS
    "${MMFG_TEST_COMPONENT_ROOT}/src"
    "${MMFG_TEST_COMPONENT_ROOT}/src/CDetour"
    "${MMFG_TEST_COMPONENT_ROOT}/include"
    "${MMFG_TEST_COMPONENT_ROOT}/include/HLSDK/common"
    "${MMFG_TEST_COMPONENT_ROOT}/include/HLSDK/dlls"
    "${MMFG_TEST_COMPONENT_ROOT}/include/HLSDK/engine"
    "${MMFG_TEST_COMPONENT_ROOT}/include/HLSDK/pm_shared"
    "${CMAKE_CURRENT_LIST_DIR}")

# Production translation units shared by the tests that link the plugin list and
# the plugin lifecycle code, followed by the shared stubs.
set(MMFG_TEST_PLUGIN_SOURCES
    "${MMFG_TEST_COMPONENT_ROOT}/src/mplugin.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/mlist.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/mreg.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/support_meta.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/log_meta.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/osdep.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/osdep_p.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/mplayer.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/conf_meta.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/meta_eiface.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/sdk_util.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/reg_support.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mm_test_stubs.cpp")

# Everything the plugin code needs, minus the plugin classes themselves. Used by
# the tests that only exercise game directory and gamedll handling.
set(MMFG_TEST_CORE_SOURCES
    "${MMFG_TEST_COMPONENT_ROOT}/src/support_meta.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/log_meta.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/osdep.cpp"
    "${MMFG_TEST_COMPONENT_ROOT}/src/mplayer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mm_test_stubs.cpp")

option(MMFG_METAMOD_TESTS_SANITIZE
    "Build the component tests with Address and UndefinedBehaviour sanitizers (GCC/Clang only)" ON)
if(MSVC)
    set(MMFG_METAMOD_TESTS_SANITIZE OFF)
endif()

# mmetest_add(<name> [LINUX_ONLY] [NO_UBSAN] SOURCES <sources...>)
#
# LINUX_ONLY keeps a test out of the Windows suite when the code under test is
# POSIX-only (directory iteration, ELF symbol resolution, ...).
#
# NO_UBSAN keeps the undefined behaviour sanitizer off for a test whose code
# under test performs intentional unaligned loads (the x86 jump forwarders).
function(mmetest_add name)
    cmake_parse_arguments(ARG "LINUX_ONLY;NO_UBSAN" "" "SOURCES" ${ARGN})
    if(ARG_LINUX_ONLY AND NOT UNIX)
        return()
    endif()
    add_executable(${name} ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE ${MMFG_TEST_INCLUDE_DIRS})
    target_compile_definitions(${name} PRIVATE __METAMOD_BUILD__
        $<$<NOT:$<CONFIG:Debug>>:__INTERNALS_USE_REGPARAMS__>)
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
    if(MSVC)
        # Keep only the functions each test actually reaches in the image.
        target_compile_options(${name} PRIVATE /Gy)
        target_link_options(${name} PRIVATE /OPT:REF)
    else()
        target_compile_options(${name} PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(${name} PRIVATE -Wl,--gc-sections)
        if(MMFG_METAMOD_TESTS_SANITIZE)
            # The lifetime and bounds defects these tests guard against are only
            # observable through the sanitizers. GCC needs the explicit
            # use-after-scope instrumentation for the stack lifetime checks.
            if(ARG_NO_UBSAN)
                set(MMFG_TEST_SANITIZE_FLAGS -fsanitize=address)
            else()
                set(MMFG_TEST_SANITIZE_FLAGS -fsanitize=address,undefined)
            endif()
            target_compile_options(${name} PRIVATE
                ${MMFG_TEST_SANITIZE_FLAGS} -fsanitize-address-use-after-scope -fno-omit-frame-pointer)
            target_link_options(${name} PRIVATE ${MMFG_TEST_SANITIZE_FLAGS})
        endif()
        target_link_libraries(${name} PRIVATE dl)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    # META_LOG writes metamod.log into the working directory; keep it out of the
    # source tree.
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

mmetest_add(mmfg_test_mplugin_ini
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/test_mplugin_ini.cpp"
        ${MMFG_TEST_PLUGIN_SOURCES})

mmetest_add(mmfg_test_mlist_add
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/test_mlist_add.cpp"
        ${MMFG_TEST_PLUGIN_SOURCES})

mmetest_add(mmfg_test_client_cvar
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/test_client_cvar.cpp"
        ${MMFG_TEST_PLUGIN_SOURCES})

mmetest_add(mmfg_test_mplayer_assignment
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/test_mplayer_assignment.cpp"
        ${MMFG_TEST_CORE_SOURCES})

mmetest_add(mmfg_test_api_hook
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/test_api_hook.cpp"
        "${MMFG_TEST_COMPONENT_ROOT}/src/api_hook.cpp"
        "${MMFG_TEST_COMPONENT_ROOT}/src/api_info.cpp"
        ${MMFG_TEST_PLUGIN_SOURCES})
target_compile_definitions(mmfg_test_api_hook PRIVATE MMFG_TEST_REAL_API_INFO)
add_library(mmfg_test_hook_plugin SHARED "${CMAKE_CURRENT_LIST_DIR}/fake_hook_plugin.cpp")
target_include_directories(mmfg_test_hook_plugin PRIVATE ${MMFG_TEST_INCLUDE_DIRS})
set_target_properties(mmfg_test_hook_plugin PROPERTIES PREFIX "")
if(MSVC)
    target_compile_definitions(mmfg_test_hook_plugin PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_definitions(mmfg_test_hook_plugin PRIVATE LINUX _LINUX)
endif()
add_dependencies(mmfg_test_api_hook mmfg_test_hook_plugin)
target_compile_definitions(mmfg_test_api_hook PRIVATE
    "MMFG_TEST_HOOK_PLUGIN=\"$<TARGET_FILE:mmfg_test_hook_plugin>\"")

# Optional comparison using an earlier dispatcher with the same harness,
# support code and compiler flags. Keep the exported source in the build tree.
set(MMFG_METAMOD_HOOK_BASELINE_SOURCE "" CACHE FILEPATH
    "Earlier api_hook.cpp to compare with the current dispatcher")
if(MMFG_METAMOD_HOOK_BASELINE_SOURCE)
    mmetest_add(mmfg_test_api_hook_baseline
        SOURCES "${CMAKE_CURRENT_LIST_DIR}/test_api_hook.cpp"
            "${MMFG_METAMOD_HOOK_BASELINE_SOURCE}"
            "${MMFG_TEST_COMPONENT_ROOT}/src/api_info.cpp"
            ${MMFG_TEST_PLUGIN_SOURCES})
    target_compile_definitions(mmfg_test_api_hook_baseline PRIVATE MMFG_TEST_REAL_API_INFO
        "MMFG_TEST_HOOK_PLUGIN=\"$<TARGET_FILE:mmfg_test_hook_plugin>\"")
    add_dependencies(mmfg_test_api_hook_baseline mmfg_test_hook_plugin)
endif()

if(UNIX)
    target_compile_definitions(mmfg_test_api_hook PRIVATE MMFG_TEST_WRAP_REALLOC)
    target_link_options(mmfg_test_api_hook PRIVATE -Wl,--wrap=realloc)
    mmetest_add(mmfg_test_config_reload
        LINUX_ONLY
        SOURCES "${CMAKE_CURRENT_LIST_DIR}/test_config_reload.cpp"
            "${MMFG_TEST_COMPONENT_ROOT}/src/conf_meta.cpp"
            ${MMFG_TEST_CORE_SOURCES})

    mmetest_add(mmfg_test_cvar_allocation
        LINUX_ONLY
        SOURCES "${CMAKE_CURRENT_LIST_DIR}/test_cvar_allocation.cpp"
            ${MMFG_TEST_PLUGIN_SOURCES})
    target_link_options(mmfg_test_cvar_allocation PRIVATE
        -Wl,--wrap=calloc -Wl,--wrap=strdup -Wl,--wrap=free)

    # Fixture for the gamedll tests: a shared object that satisfies the ELF
    # symbol check in osdep_detect_gamedll_linux.cpp.
    add_library(mmfg_test_fake_gamedll SHARED "${CMAKE_CURRENT_LIST_DIR}/fake_gamedll.c")
    set_target_properties(mmfg_test_fake_gamedll PROPERTIES
        PREFIX ""
        OUTPUT_NAME "mmfg_fake_gamedll")

    mmetest_add(mmfg_test_elf_boundary
        LINUX_ONLY
        SOURCES "${CMAKE_CURRENT_LIST_DIR}/test_elf_boundary.cpp"
            "${MMFG_TEST_COMPONENT_ROOT}/src/osdep_detect_gamedll_linux.cpp"
            ${MMFG_TEST_CORE_SOURCES})
    add_dependencies(mmfg_test_elf_boundary mmfg_test_fake_gamedll)
    target_compile_definitions(mmfg_test_elf_boundary PRIVATE
        "MMFG_TEST_FAKE_GAMEDLL=\"$<TARGET_FILE:mmfg_test_fake_gamedll>\"")

    mmetest_add(mmfg_test_mplugin_attach
        LINUX_ONLY
        SOURCES "${CMAKE_CURRENT_LIST_DIR}/test_mplugin_attach.cpp"
            ${MMFG_TEST_PLUGIN_SOURCES})
    target_link_options(mmfg_test_mplugin_attach PRIVATE -rdynamic)
    add_dependencies(mmfg_test_mplugin_attach mmfg_test_fake_gamedll)
    target_compile_definitions(mmfg_test_mplugin_attach PRIVATE
        "MMFG_TEST_FAKE_GAMEDLL=\"$<TARGET_FILE:mmfg_test_fake_gamedll>\"")

    mmetest_add(mmfg_test_game_autodetect
        LINUX_ONLY
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/test_game_autodetect.cpp"
            "${MMFG_TEST_COMPONENT_ROOT}/src/game_autodetect.cpp"
            "${MMFG_TEST_COMPONENT_ROOT}/src/osdep_detect_gamedll_linux.cpp"
            ${MMFG_TEST_CORE_SOURCES})
    add_dependencies(mmfg_test_game_autodetect mmfg_test_fake_gamedll)
    target_compile_definitions(mmfg_test_game_autodetect PRIVATE
        "MMFG_TEST_FAKE_GAMEDLL=\"$<TARGET_FILE:mmfg_test_fake_gamedll>\"")

    mmetest_add(mmfg_test_game_support
        LINUX_ONLY
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/test_game_support.cpp"
            "${MMFG_TEST_COMPONENT_ROOT}/src/game_support.cpp"
            "${MMFG_TEST_COMPONENT_ROOT}/src/game_autodetect.cpp"
            "${MMFG_TEST_COMPONENT_ROOT}/src/osdep_detect_gamedll_linux.cpp"
            "${MMFG_TEST_COMPONENT_ROOT}/src/conf_meta.cpp"
            ${MMFG_TEST_CORE_SOURCES})

    # The test includes osdep_linkent_linux.cpp itself: the code under test has
    # internal linkage.
    mmetest_add(mmfg_test_osdep_linkent_linux
        LINUX_ONLY
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/test_osdep_linkent_linux.cpp"
            ${MMFG_TEST_CORE_SOURCES})
    target_link_libraries(mmfg_test_osdep_linkent_linux PRIVATE pthread)
endif()

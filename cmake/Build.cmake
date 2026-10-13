include_guard(GLOBAL)

function(mmfg_configure_install_layout)
    if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        return()
    endif()
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE)
            set(config "${CMAKE_BUILD_TYPE}")
        else()
            # Multi-config install prefixes cannot expand $<CONFIG>. Use one
            # build/x86/<Config> tree per configuration instead.
            get_filename_component(config "${CMAKE_BINARY_DIR}" NAME)
            if(NOT config MATCHES "^(Debug|Release)$")
                set(config Release)
            endif()
        endif()
        set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install/x86/${config}"
            CACHE PATH "Install prefix for the selected x86 configuration" FORCE)
    endif()
    message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
endfunction()

function(mmfg_configure_target target)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "${target} requires 32-bit compilation; use -A Win32 on Windows.")
    endif()
    target_compile_features(${target} PRIVATE cxx_std_17)
    set_target_properties(${target} PROPERTIES PREFIX "" DEBUG_POSTFIX "" POSITION_INDEPENDENT_CODE ON)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_definitions(${target} PRIVATE WIN32 _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS
            $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
        target_compile_options(${target} PRIVATE /Zi)
        target_link_options(${target} PRIVATE /DEBUG)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(${target} PRIVATE PLATFORM_POSIX LINUX _LINUX
            $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
        target_compile_options(${target} PRIVATE -m32 -fno-exceptions -fno-rtti -fvisibility=hidden
            -march=i686 -mtune=generic -msse -msse2 -g $<$<NOT:$<CONFIG:Debug>>:-O2>)
        target_link_options(${target} PRIVATE -m32 -static-libgcc -static-libstdc++)
        target_link_libraries(${target} PRIVATE dl m)
    else()
        message(FATAL_ERROR "Supported platforms are Windows MSVC Win32 and Linux i386.")
    endif()
    install(TARGETS ${target} RUNTIME DESTINATION addons/metamod/dlls
        LIBRARY DESTINATION addons/metamod/dlls)
    if(MSVC)
        install(FILES "$<TARGET_PDB_FILE:${target}>" DESTINATION addons/metamod/dlls OPTIONAL)
    endif()
endfunction()

# Synchronize a plugin's GoldSrc_VibeSignatures gamedata at build time: the
# manifest selects the symbols to keep, the pruned catalog is validated against
# it and installed to addons/metamod/gamedata/<target>, where metamod merges it
# into its catalog. MMFG_SYNC_GAMEDATA=OFF only installs an existing catalog.
function(mmfg_add_gamedata target manifest)
    option(MMFG_SYNC_GAMEDATA "Download and prune GoldSrc_VibeSignatures gamedata at build time (requires Python 3.8+)" ON)
    get_filename_component(manifest "${manifest}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(gamedata_dir "${PROJECT_BINARY_DIR}/assets/addons/metamod/gamedata/${target}")
    if(MMFG_SYNC_GAMEDATA)
        find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
        set(script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/sync-gamedata.py")
        add_custom_target(${target}_gamedata
            COMMAND "${Python3_EXECUTABLE}" -B "${script}" --manifest "${manifest}"
                --target-dir "${gamedata_dir}" --temp-root "${PROJECT_BINARY_DIR}/gamedata-sync"
            COMMAND "${Python3_EXECUTABLE}" -B "${script}" --manifest "${manifest}"
                --target-dir "${gamedata_dir}" --validate-only
            COMMENT "Synchronizing ${target} gamedata"
            VERBATIM)
        add_dependencies(${target} ${target}_gamedata)
    endif()
    install(DIRECTORY "${gamedata_dir}/" DESTINATION addons/metamod/gamedata/${target} OPTIONAL)
endfunction()

# Visual Studio filters mirror each source's directory under the tree that owns
# it: this repository, the build tree, or a dependency's <NAME>_SOURCE_PATH. The
# longest matching root wins, because fetched dependencies can live inside the
# build tree and the build tree inside this repository.
function(mmfg_assign_source_groups target)
    set(roots "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")
    set(labels "." "Generated")
    get_cmake_property(variables VARIABLES)
    list(FILTER variables INCLUDE REGEX "_SOURCE_PATH$")
    foreach(variable IN LISTS variables)
        if(${variable})
            get_filename_component(root "${${variable}}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
            get_filename_component(label "${root}" NAME)
            # FetchContent trees are named mmfg_<dependency>-src.
            string(REGEX REPLACE "^[a-z0-9]+_(.+)-src$" "\\1" label "${label}")
            list(APPEND roots "${root}")
            list(APPEND labels "${label}")
        endif()
    endforeach()
    list(LENGTH roots root_count)
    math(EXPR last_root "${root_count} - 1")

    get_target_property(source_dir ${target} SOURCE_DIR)
    get_target_property(sources ${target} SOURCES)
    foreach(source IN LISTS sources)
        if(source MATCHES "^\$<")
            continue()
        endif()
        get_filename_component(path "${source}" ABSOLUTE BASE_DIR "${source_dir}")
        set(group "External")
        set(best_length -1)
        foreach(index RANGE ${last_root})
            list(GET roots ${index} root)
            string(LENGTH "${root}" length)
            cmake_path(IS_PREFIX root "${path}" NORMALIZE inside)
            if(inside AND length GREATER best_length)
                list(GET labels ${index} label)
                file(RELATIVE_PATH relative "${root}" "${path}")
                get_filename_component(relative "${relative}" DIRECTORY)
                if(label STREQUAL ".")
                    set(group "${relative}")
                else()
                    string(JOIN "/" group ${label} ${relative})
                endif()
                set(best_length ${length})
            endif()
        endforeach()
        # Files at the repository root keep the generator's default filter.
        if(group)
            source_group("${group}" FILES "${path}")
        endif()
    endforeach()
endfunction()

# eatlead - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
# The release number, shown in the log's first line (version_log.cpp).
project(eatlead VERSION 0.0.1 LANGUAGES CXX)
set(EATLEAD_RELEASE "0.0.1")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(EATLEAD_SOURCES
    src/main.cpp
    src/gpu_preload.cpp
    src/crash_win.cpp
    src/version_log.cpp
    src/frame_pacing.cpp
    src/fps_unlock.cpp
    src/achievement_list.cpp
    src/menu_trace.cpp
    src/menu_quit.cpp
    src/pad_prompts.cpp
)

if(WIN32)
    add_executable(eatlead WIN32 ${EATLEAD_SOURCES})
else()
    add_executable(eatlead ${EATLEAD_SOURCES})
endif()

rexglue_setup_target(eatlead GPU_PLUGINS xenos)
target_compile_definitions(eatlead PRIVATE EATLEAD_VERSION="${EATLEAD_RELEASE}")

# Skullburglar's native renderer plugin (--gpu_plugin=native, the default), when the SDK was built
# with it (Bourne's tools/build_native.bat). Checked when the build runs, so building the plugin
# later needs no reconfigure.
get_filename_component(EATLEAD_SDK_BIN "${rexglue_DIR}/../../../bin" ABSOLUTE)
file(WRITE ${CMAKE_BINARY_DIR}/copy_if_exists.cmake
    "if(EXISTS \"\${SRC}\")\n  file(COPY \"\${SRC}\" DESTINATION \"\${DST}\")\nendif()\n")
add_custom_command(TARGET eatlead POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DSRC=${EATLEAD_SDK_BIN}/rexgpu-native$<$<CONFIG:RelWithDebInfo>:rd>$<$<CONFIG:Debug>:d>.dll"
        "-DDST=$<TARGET_FILE_DIR:eatlead>"
        -P ${CMAKE_BINARY_DIR}/copy_if_exists.cmake
    VERBATIM)

# AMD FidelityFX runtime (FSR / CAS output filters), when the SDK was built with it.
file(GLOB EATLEAD_FFX_DLLS "${EATLEAD_SDK_BIN}/amd_fidelityfx_dx12*.dll")
if(EATLEAD_FFX_DLLS)
    add_custom_command(TARGET eatlead POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EATLEAD_FFX_DLLS} $<TARGET_FILE_DIR:eatlead>
        COMMENT "Copying FidelityFX runtime")
endif()

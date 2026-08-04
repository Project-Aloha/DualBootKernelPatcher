cmake_minimum_required(VERSION 3.18)
project(MultiBootKernelPatcher C)

option(BUILD_SHIM_LOADER "Build the ARM64 ShimLoader binary" ON)
option(BUILD_SHIMS "Build the simplified ARM64 Shims" ${BUILD_SHIM_LOADER})

if (BUILD_SHIM_LOADER OR BUILD_SHIMS)
    if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        # MinGW compiler download link:
        # https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
        set(ARM64_TOOLCHAIN_PREFIX
            "E:/aarch64-gcc/aarch64-none-elf/bin/"
            CACHE STRING "ARM64 binutils path and executable prefix")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(ARM64_TOOLCHAIN_PREFIX "aarch64-linux-gnu-"
            CACHE STRING "ARM64 binutils path and executable prefix")
    else ()
        message(FATAL_ERROR
            "Building ARM64 Shim binaries on ${CMAKE_HOST_SYSTEM_NAME} is not supported")
    endif ()

    set(ARM64_AS "${ARM64_TOOLCHAIN_PREFIX}as"
        CACHE FILEPATH "ARM64 assembler")
    set(ARM64_OBJCOPY "${ARM64_TOOLCHAIN_PREFIX}objcopy"
        CACHE FILEPATH "ARM64 objcopy")
endif ()

set(CMAKE_C_STANDARD 11)
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif ()

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/inih/ini.c)
    message(FATAL_ERROR
        "The inih submodule is missing. Run: git submodule update --init --recursive")
endif ()

add_library(inih_static STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/inih/ini.c
)
if (MSVC)
    set_property(TARGET inih_static PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()
target_include_directories(inih_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/inih
)
set(INIH_TARGET inih_static)

# Compile the patcher.
add_executable(MultiBootKernelPatcher patcher.c shim.c shim_config.c utils.c)
if (MSVC)
    set_property(TARGET MultiBootKernelPatcher PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()
target_link_libraries(MultiBootKernelPatcher PRIVATE ${INIH_TARGET})
add_executable(HDRTool HDRTool.c utils.c)
if (MSVC)
    set_property(TARGET HDRTool PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

if (BUILD_SHIM_LOADER)
    add_subdirectory(ShimLoader)
endif ()
if (BUILD_SHIMS)
    add_subdirectory(Shims)
endif ()
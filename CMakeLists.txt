cmake_minimum_required(VERSION 3.18)
project(MultiBootKernelPatcher C)

option(BUILD_SHIM_LOADER "Build the ARM64 ShimLoader binary" ON)
option(BUILD_SHIMS "Build the simplified ARM64 Shims" ${BUILD_SHIM_LOADER})

find_package(unofficial-inih CONFIG QUIET)
if (TARGET unofficial::inih::libinih)
    set(INIH_TARGET unofficial::inih::libinih)
else ()
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(INIH QUIET IMPORTED_TARGET inih)
    endif ()
    if (TARGET PkgConfig::INIH)
        set(INIH_TARGET PkgConfig::INIH)
    else ()
        message(FATAL_ERROR
            "inih was not found. Install libinih-dev on Debian/Ubuntu, "
            "inih-devel on Fedora, or configure CMake with the vcpkg "
            "toolchain; vcpkg.json declares the inih dependency.")
    endif ()
endif ()

set(CMAKE_C_STANDARD 11)
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_EXE_LINKER_FLAGS "-static")
endif ()
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

# Compile the patcher.
add_executable(MultiBootKernelPatcher patcher.c shim.c shim_config.c utils.c)
target_link_libraries(MultiBootKernelPatcher PRIVATE ${INIH_TARGET})
add_executable(HDRTool HDRTool.c utils.c)

if (BUILD_SHIM_LOADER)
    add_subdirectory(ShimLoader)
endif ()
if (BUILD_SHIMS)
    add_subdirectory(Shims)
endif ()
cmake_minimum_required(VERSION 3.18)
project(DualBootKernelPatcher C)

set(CMAKE_C_STANDARD 11)
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_EXE_LINKER_FLAGS "-static")
endif ()

# Compile the patcher.
add_executable(DualBootKernelPatcher patcher.c utils.c)
add_executable(DualBootPatchRemover remover.c utils.c)
add_executable(HDRTool HDRTool.c utils.c)

# Compile Shell Codes with aarch64 gcc.
add_subdirectory(ShellCode)
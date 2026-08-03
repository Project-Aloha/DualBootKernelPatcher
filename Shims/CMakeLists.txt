cmake_minimum_required(VERSION 3.27)
project(Shims ASM)

# Set up compilers
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    # Windows
    # MinGW compiler download link:
    #   https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
    set(CROSS_COMPILE "E:/aarch64-gcc/aarch64-none-elf/bin/")
elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    # Linux
    set(CROSS_COMPILE "aarch64-linux-gnu-")
else ()
    message("build on ${CMAKE_HOST_SYSTEM_NAME} is not supported")
endif ()

set(CMAKE_AS ${CROSS_COMPILE}as)
set(CMAKE_OBJCOPY ${CROSS_COMPILE}objcopy)

set(SHIM_ELF_DIR "${CMAKE_CURRENT_BINARY_DIR}/elf")
file(MAKE_DIRECTORY ${SHIM_ELF_DIR})

# Compile each simplified Shim.
file(GLOB ASM_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "Shim.*.S")
set(SHIM_BIN_TARGETS)
foreach (CURRENT_ASM_SOURCE_SUFFIX ${ASM_SOURCES})
    string(REGEX REPLACE "\\.S$" "" CURRENT_ASM_SOURCE
        "${CURRENT_ASM_SOURCE_SUFFIX}")

    add_custom_target(
        ${CURRENT_ASM_SOURCE}_ELF ALL
        ${CMAKE_AS}
        "${CMAKE_CURRENT_SOURCE_DIR}/${CURRENT_ASM_SOURCE}.S"
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        -o "${SHIM_ELF_DIR}/${CURRENT_ASM_SOURCE}.elf"
    )

    add_custom_target(
        ${CURRENT_ASM_SOURCE}_BIN ALL
        ${CMAKE_OBJCOPY} -O binary
        "${SHIM_ELF_DIR}/${CURRENT_ASM_SOURCE}.elf"
        "${CURRENT_ASM_SOURCE}.bin"
        DEPENDS ${CURRENT_ASM_SOURCE}_ELF
    )
    list(APPEND SHIM_BIN_TARGETS ${CURRENT_ASM_SOURCE}_BIN)
endforeach ()

add_custom_target(Shims DEPENDS ${SHIM_BIN_TARGETS})

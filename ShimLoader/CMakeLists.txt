set(SHIM_LOADER_ELF_DIR "${CMAKE_CURRENT_BINARY_DIR}/elf")
file(MAKE_DIRECTORY ${SHIM_LOADER_ELF_DIR})

# Compile the generic ShimLoader.
set(ASM_SOURCES ShimLoader.S)
foreach (CURRENT_ASM_SOURCE_SUFFIX ${ASM_SOURCES})
    string(REGEX REPLACE "\\.S$" "" CURRENT_ASM_SOURCE
        "${CURRENT_ASM_SOURCE_SUFFIX}")

    add_custom_target(
        ${CURRENT_ASM_SOURCE}_ELF ALL
        ${ARM64_AS}
        "${CMAKE_CURRENT_SOURCE_DIR}/${CURRENT_ASM_SOURCE}.S"
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        -o "${SHIM_LOADER_ELF_DIR}/${CURRENT_ASM_SOURCE}.elf"
    )

    add_custom_target(
        ${CURRENT_ASM_SOURCE}_BIN ALL
        ${ARM64_OBJCOPY} -O binary
        "${SHIM_LOADER_ELF_DIR}/${CURRENT_ASM_SOURCE}.elf"
        "${CURRENT_ASM_SOURCE}.bin"
        DEPENDS ${CURRENT_ASM_SOURCE}_ELF
    )
endforeach ()

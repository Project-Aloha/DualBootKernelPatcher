set(SHIM_ELF_DIR "${CMAKE_CURRENT_BINARY_DIR}/elf")
file(MAKE_DIRECTORY ${SHIM_ELF_DIR})

# Compile each Shim blob.
file(GLOB ASM_SOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    "Shim.*.S")
set(SHIM_BIN_TARGETS)
foreach (CURRENT_ASM_SOURCE_SUFFIX ${ASM_SOURCES})
    string(REGEX REPLACE "\\.S$" "" CURRENT_ASM_SOURCE
        "${CURRENT_ASM_SOURCE_SUFFIX}")

    add_custom_target(
        ${CURRENT_ASM_SOURCE}_ELF ALL
        ${ARM64_AS}
        "${CMAKE_CURRENT_SOURCE_DIR}/${CURRENT_ASM_SOURCE}.S"
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        -o "${SHIM_ELF_DIR}/${CURRENT_ASM_SOURCE}.elf"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${CURRENT_ASM_SOURCE}.S"
            "${CMAKE_CURRENT_SOURCE_DIR}/DummyHead.S"
            "${CMAKE_CURRENT_SOURCE_DIR}/CommonTail.S"
    )

    add_custom_target(
        ${CURRENT_ASM_SOURCE}_BIN ALL
        ${ARM64_OBJCOPY} -O binary
        "${SHIM_ELF_DIR}/${CURRENT_ASM_SOURCE}.elf"
        "${CURRENT_ASM_SOURCE}.bin"
        DEPENDS ${CURRENT_ASM_SOURCE}_ELF
    )
    list(APPEND SHIM_BIN_TARGETS ${CURRENT_ASM_SOURCE}_BIN)
endforeach ()

add_custom_target(Shims DEPENDS ${SHIM_BIN_TARGETS})

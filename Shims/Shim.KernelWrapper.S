/* Assembly Shim for replacing the Linux DTB before entering BaseImage. */
.text
.align 2

_ShimStart:
    mov     x19, x0
    mov     x20, x1
    mov     x21, x2
    mov     x22, x3
    mov     x23, x30
    // BL leaves LR at BaseImage + 4; derive the ARM64 Linux header address.
    sub     x24, x23, #4

    // Linux header +0x20/+0x28 contain the runtime-relative Manifest offset
    // and the full Manifest blob size. Two entries require at least 180 bytes.
    ldr     x25, [x24, #0x20]
    cbz     x25, _Linux
    ldr     x8, [x24, #0x28]
    cmp     x8, #180
    b.lo    _Linux
    add     x25, x24, x25
    add     x14, x25, x8
    // ManifestHeader: magic +0x00, version +0x04 and entryCount +0x08.
    ldr     w8, [x25]
    movz    w9, #0x504b
    movk    w9, #0x464d, lsl #16
    cmp     w8, w9
    b.ne    _Linux
    ldr     w8, [x25, #4]
    mov     w9, #0x10000
    cmp     w8, w9
    b.ne    _Linux
    ldr     w8, [x25, #8]
    cmp     w8, #2
    b.lo    _Linux

    // Entry #1 starts after the variable-length BaseImage entry (#0).
    add     x25, x25, #60
    ldr     w8, [x25, #52]
    add     x25, x25, #56
    add     x25, x25, x8
    add     x25, x25, #3
    and     x25, x25, #~3
    add     x8, x25, #56
    cmp     x8, x14
    b.hi    _Linux
    // ManifestEntry: offset +0x00, size +0x08, type +0x10,
    // flags +0x14 and loadAddress +0x18.
    ldr     w8, [x25, #16]
    // MF_ENTRY_TYPE_DTB is 5; the reserved DTB entry must carry exactly
    // MF_ENTRY_FLAG_COPY (bit 1) and no other flags.
    cmp     w8, #5
    b.ne    _Linux
    ldr     w8, [x25, #20]
    cmp     w8, #2
    b.ne    _Linux

    // Copy the packed DTB from BaseImage + entry.offset to entry.loadAddress.
    ldr     x8, [x25]
    add     x8, x24, x8
    ldr     x9, [x25, #24]
    cbz     x9, _Linux
    ldr     x10, [x25, #8]
    cbz     x10, _Linux
    mov     x11, x9
    mov     x12, x10
    bl      _CopyLoop

    // Clean the replacement DTB range before returning to the Linux branch.
    mov     x0, x11
    mov     x1, x12
    bl      _SyncDataCache

    mov     x0, x11
    // Return through the original Linux branch with replacement FDT in x0.
    mov     x1, x20
    mov     x2, x21
    mov     x3, x22
    mov     x30, x23
    ret

_Linux:
    mov     x0, x19
    mov     x1, x20
    mov     x2, x21
    mov     x3, x22
    mov     x30, x23
    ret

_CopyLoop:
    // x8 = source, x9 = destination, x10 = remaining bytes.
    cbz     x10, 2f
1:
    ldrb    w13, [x8], #1
    strb    w13, [x9], #1
    subs    x10, x10, #1
    b.ne    1b
2:
    ret

// x0 = destination, x1 = size
_SyncDataCache:
    mrs     x2, ctr_el0
    ubfx    x3, x2, #16, #4
    mov     x4, #4
    lsl     x3, x4, x3
    sub     x5, x3, #1
    bic     x4, x0, x5
    add     x5, x0, x1
3:
    dc      cvac, x4
    add     x4, x4, x3
    cmp     x4, x5
    b.lo    3b
    dsb     sy
    ret

.text
.align 4

_ShimImageEnd:
/* Do not remove the last line  */

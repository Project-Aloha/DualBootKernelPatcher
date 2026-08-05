// Shared implementation for two-entry assembly Shims. The entry BL leaves
// x30 at BaseImage + 4; x23 preserves it while the Shim selects an image.

_Linux:
    mov     x0, x19
    mov     x1, x20
    mov     x2, x21
    mov     x3, x22
    mov     x30, x23
    ret

_UEFI:
    // BL leaves LR at BaseImage + 4. The ARM64 Linux header starts at BaseImage.
    sub     x24, x23, #4
    // Linux header +0x20: runtime-relative Manifest offset. Zero means absent.
    ldr     x25, [x24, #0x20]
    cbz     x25, _Linux
    // Linux header +0x28: full v7 Manifest blob size. A two-entry v7 blob is
    // 384 bytes: one 128-byte header and two 128-byte entries.
    ldr     x8, [x24, #0x28]
    cmp     x8, #384
    b.lo    _Linux
    add     x25, x24, x25

    // ShimManifestHeader: magic +0x00, version +0x08, headerSize +0x0c,
    // entrySize +0x10 and entryCount +0x14.
    ldr     x8, [x25]
    ldr     x9, _ManifestMagic
    cmp     x8, x9
    b.ne    _Linux
    ldr     w8, [x25, #8]
    cmp     w8, #7
    b.ne    _Linux
    ldr     w8, [x25, #12]
    cmp     w8, #128
    b.ne    _Linux
    ldr     w8, [x25, #16]
    cmp     w8, #128
    b.ne    _Linux
    ldr     w8, [x25, #20]
    cmp     w8, #2
    b.lo    _Linux

    // Entry #1 follows the 128-byte header and BaseImage entry (#0).
    // This fixed UEFI path expects entry #1 to be an executable payload.
    add     x25, x25, #128
    add     x25, x25, #128
    // ShimManifestEntry: offset +0x20, size +0x28, type +0x30,
    // flags +0x34, loadAddress +0x38 and entryOffset +0x40.
    ldr     w8, [x25, #48]
    cbz     w8, _Linux
    cmp     w8, #3
    b.hi    _Linux
    ldr     x10, [x25, #40]
    cmp     x10, #4
    b.lo    _Linux
    ldr     x11, [x25, #64]
    sub     x12, x10, #4
    cmp     x11, x12
    b.hi    _Linux

    // Source address is BaseImage plus the payload's runtime-relative offset.
    ldr     x8, [x25, #32]
    add     x8, x24, x8
    ldr     w9, [x25, #52]
    // SHIM_ENTRY_FLAG_COPY is bit 1. Copy before executing when it is set.
    tbz     w9, #1, _ExecuteEntry
    ldr     x9, [x25, #56]
    cbz     x9, _Linux
    mov     x24, x9
    mov     x12, x10
    bl      _CopyLoop
    // Clean and invalidate the copied executable range before branching to it.
    mov     x0, x24
    mov     x1, x12
    bl      _SyncImageCache
    mov     x8, x24

_ExecuteEntry:
    // Execute payloadBase + entryOffset. Preserve the established payload ABI:
    // x0/x1 identify the end of this Shim image and x2/x3 retain Linux args.
    add     x24, x8, x11
    adr     x0, _ShimImageEnd
    adr     x1, _ShimImageEnd
    mov     x2, x21
    mov     x3, x22
    br      x24

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
_SyncImageCache:
    mrs     x2, ctr_el0
    ubfx    x3, x2, #16, #4
    mov     x5, #4
    lsl     x3, x5, x3
    sub     x6, x3, #1
    bic     x5, x0, x6
    add     x6, x0, x1
3:
    dc      cvau, x5
    add     x5, x5, x3
    cmp     x5, x6
    b.lo    3b
    dsb     ish

    and     x3, x2, #0xf
    mov     x5, #4
    lsl     x3, x5, x3
    sub     x7, x3, #1
    bic     x5, x0, x7
4:
    ic      ivau, x5
    add     x5, x5, x3
    cmp     x5, x6
    b.lo    4b
    dsb     ish
    isb
    ret

_Dead:
    b       _Dead

.align 3
_ManifestMagic:
    .ascii  "SHIMMF\0\0"

.text
.align 4

_ShimImageEnd:
/* Do not remove the last line  */

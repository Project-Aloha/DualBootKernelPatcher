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
    sub     x24, x23, #4
    ldr     x25, [x24, #0x20]
    cbz     x25, _Linux
    ldr     x8, [x24, #0x28]
    cmp     x8, #448
    b.lo    _Linux
    add     x25, x24, x25

    ldr     x8, [x25]
    ldr     x9, _ManifestMagic
    cmp     x8, x9
    b.ne    _Linux
    ldr     w8, [x25, #8]
    cmp     w8, #5
    b.ne    _Linux
    ldr     w8, [x25, #12]
    cmp     w8, #64
    b.ne    _Linux
    ldr     w8, [x25, #16]
    cmp     w8, #80
    b.ne    _Linux
    ldr     w8, [x25, #20]
    cmp     w8, #2
    b.lo    _Linux

    add     x25, x25, #64
    add     x25, x25, #80
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

    ldr     x8, [x25, #32]
    add     x8, x24, x8
    ldr     w9, [x25, #52]
    tbz     w9, #1, _ExecuteEntry
    ldr     x9, [x25, #56]
    cbz     x9, _Linux
    mov     x24, x9
    mov     x12, x10
    bl      _CopyLoop
    mov     x0, x24
    mov     x1, x12
    bl      _SyncImageCache
    mov     x8, x24

_ExecuteEntry:
    add     x24, x8, x11
    adr     x0, _ShimImageEnd
    adr     x1, _ShimImageEnd
    mov     x2, x21
    mov     x3, x22
    br      x24

_CopyLoop:
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

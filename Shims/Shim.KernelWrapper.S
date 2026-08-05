/* Assembly Shim for replacing the Linux DTB before entering BaseImage. */
.include "DummyHead.S"

_ShimStart:
    mov     x19, x0
    mov     x20, x1
    mov     x21, x2
    mov     x22, x3
    mov     x23, x30
    sub     x24, x23, #4

    ldr     x25, [x24, #0x20]
    cbz     x25, _Linux
    ldr     x8, [x24, #0x28]
    cmp     x8, #224
    b.lo    _Linux
    add     x25, x24, x25
    ldr     x8, [x25]
    ldr     x9, _ManifestMagic
    cmp     x8, x9
    b.ne    _Linux
    ldr     w8, [x25, #8]
    cmp     w8, #4
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
    cmp     w8, #5
    b.ne    _Linux
    ldr     w8, [x25, #52]
    tbz     w8, #1, _Linux

    ldr     x8, [x25, #32]
    add     x8, x24, x8
    ldr     x9, [x25, #56]
    cbz     x9, _Linux
    ldr     x10, [x25, #40]
    cbz     x10, _Linux
    mov     x11, x9
    mov     x12, x10
    bl      _CopyLoop

    mov     x0, x11
    mov     x1, x12
    bl      _SyncDataCache

    mov     x0, x11
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

.align 3
_ManifestMagic:
    .ascii  "SHIMMF\0\0"

.text
.align 4

_ShimImageEnd:
/* Do not remove the last line  */

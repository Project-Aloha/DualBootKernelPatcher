/*
 * Simplified assembly Shim for bypassing Android DTB compatibility checks.
 * The packer writes payload offset, CopyTo and actual payload size directly
 * into this Shim Image header. No manifest parsing is performed here.
 */
.include "DummyHead.S"

_ShimStart:
    mov     x19, x0
    mov     x20, x1
    mov     x21, x2
    mov     x22, x3
    mov     x23, x4

    ldr     x8, _PayloadOffset
    add     x8, x23, x8
    ldr     x9, _CopyAddress
    ldr     x10, _CopySize
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
    add     x8, x23, #4
    br      x8

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

.text
.align 4

_ShimImageEnd:
/* Do not remove the last line  */

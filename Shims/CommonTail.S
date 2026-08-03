// Shared implementation for simplified assembly Shims. The ShimLoader ABI
// supplies x4 = base Linux and x5/x6 = manifest address/size; simplified
// Shims intentionally ignore x5/x6 and use direct header parameters instead.

_Linux:
    mov     x0, x19
    mov     x1, x20
    mov     x2, x21
    mov     x3, x22
    mov     x8, x23
    add     x8, x8, #4
    br      x8

_UEFI:
    ldr     x8, _PayloadOffset
    add     x8, x23, x8
    ldr     x9, _CopyAddress
    ldr     x10, _CopySize
    mov     x11, x9
    mov     x12, x10
    bl      _CopyLoop
    mov     x0, x11
    mov     x1, x12
    bl      _SyncImageCache
    adr     x0, _ShimImageEnd
    adr     x1, _ShimImageEnd
    br      x11

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

.text
.align 4

_ShimImageEnd:
/* Do not remove the last line  */

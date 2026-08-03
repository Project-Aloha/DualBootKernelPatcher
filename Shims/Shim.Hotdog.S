.include "DummyHead.S"

/* Simplified Shim for Oneplus 7T Pro */
_ShimStart:
    mov     x19, x0
    mov     x20, x1
    mov     x21, x2
    mov     x22, x3
    mov     x23, x4
    movz    x4, #0xd000
    movk    x4, #0x317, lsl #16     // Store 0x317d004 to x4, which is control reg of gpio 125.
    str     wzr, [x4], #4           // Set 0 in control reg, then x4 add 4
    ldr     w5, [x4]                // Get status value from address stored in x4 and store it to w5.
    and     w5, w5, #1              // w5 & 1 and save value in w5.
    cbz     w5, _UEFI               // Compare w5 with 0, if w5 == 0 goto _UEFI, else execute next instruction(jmp Linux).

.include "CommonTail.S"
/* Do not remove the last line  */

.include "DummyHead.S"

/* Simplified Shim for Xiaomi 9 */
_ShimStart:
    mov     x19, x0
    mov     x20, x1
    mov     x21, x2
    mov     x22, x3
    mov     x23, x4
    movz    x4, #0x1000
    movk    x4, #0x396, lsl #16     // Store 0x3961004 to x4, which is io status reg of gpio 97.
    ldr     w5, [x4]
    orr     w5, w5, #0x3            // w5 |= 0x3
    str     w5, [x4], #4            // Set w5 in control reg, then x4 add 4
    /* Wait 3 instructions here. */
    ldr     w5, [x4]                // Get status value from address stored in x4 and store it to w5.
    ldr     w5, [x4]                // Get status value from address stored in x4 and store it to w5.
    ldr     w5, [x4]                // Get status value from address stored in x4 and store it to w5.
    and     w5, w5, #1              // w5 & 1 and save value in w5.
    cbz     w5, _UEFI               // Compare w5 with 0, if w5 == 0 goto _UEFI, else execute next instruction(jmp Linux).

.include "CommonTail.S"
/* Do not remove the last line  */

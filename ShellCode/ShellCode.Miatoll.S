.include "DummyHead.S"

/* Shell Code for Redmi Note 9S */
_ShellCodeStart:
    movz    x4, #0x6000
    movk    x4, #0xE60, lsl #16     // Store 0xE606000 to x4, which is address of 0xC0 spmi channel
    movz    x5, #0x0081
    movk    x5, #0x800, lsl #16     // Store 0x8000081 to x5, which is command to read gpio status
    str     x5, [x4], 0x8           // Store x5 to cmd reg, then add 0x8 to x4
    mov     w7, wzr                 // Set w7 to 0
WaitForDone:
    ldr     w7, [x4]                // Get status value from address stored in x4 and store it to w7
    cbz     w7, WaitForDone         // Compare w7 with 0x0, if w7 == 0x0 goto WaitForDone
    add     x4, x4, 0x10            // Add 0x10 to x4
    mov     w6, wzr                 // Set w6 to 0
WaitForData:
    ldr     w6, [x4]                // Get data value from address stored in x4 and store it to w6
    cbz     w6, WaitForData         // Compare w6 with 0x0, if w6 == 0x0 goto WaitForData
    and     w6, w6, 0x1             // w6 & 0x1 and save value in w6.
    cbz     w6, _UEFI               // Compare w6 with 0, if w6 == 0 goto _UEFI, else execute next instruction(jmp Linux)

.include "CommonTail.S"
/* Do not remove the last line  */

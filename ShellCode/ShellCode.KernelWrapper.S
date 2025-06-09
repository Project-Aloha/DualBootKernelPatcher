/**
* A Wrapper for Linux Kernel to by pass android dtb check
*
*   Qualcomm application bootloader (ABL) will check msm-id/board-id
*   or apply overlay before booting kernel. By replacing android dtb with linux dtb,
*   abl will failed to check or apply dtbo and then refuse to boot.
*   This wrapper is used to bypass the check.
*
*   Scheme:
*      Only replace kernel instead of replacing dtb. But inject the dtb in kernel
*
*   Step:
*      Inject this wrapper into kernel header and make a payload with linux kernel + linux dtb.
*      Then, repack the android boot image with the payload, but not replace dtb.
*      Linux will boot successfully then.
*
*   Note:
*      You need to add memory region for linux dtb in device tree manually
*      otherwise linux will NOT boot. check the sample below, you can find
*      it in android fdt at /sys/firmware/fdt.
*       memory {
*           ddr_device_type = <0x08>;
*           device_type = "memory";
*           #address-cells = <2>;
*           #size-cells = <2>;
*           reg = 	<0x0 0x80000000 0x0 0x3b800000>,
*                   <0x1 0x80000000 0x1 0x00000000>,
*                   <0x0 0xc0000000 0x0 0xc0000000>;
*       };
*
*   Usage:
*      ./DualBootKernelPatcher OriginalKernel mainline_dtb output DualBoot.Sm8250DT.cfg ShellCode.KernelWrapper.bin
*      Then repack android image with the output file.
*
*   Inspired by @bigfootACA
*
*/

/* Dummy Header for shellcode */
.include "DummyHead.S"

_ShellCodeStart:
    // Calculate UEFI FD(dtb addr here) start address and store in X4
    adr     x4, _KernelHead         // Store kernel head address in x4.
    ldr     x5, _KernelSize         // Store kernel size in x5.
    add     x4, x4, x5              // Add kernel base + kernel size, store value in x4.

    // Copy dtb to safe place (StackRegion, you can configure it freely in DualBoot config file)
    ldr     x5, _StackBase          // Store FD Base in x5.
    ldr     x6, _StackSize          // Store FD Size in x6.
    bl      _CopyLoop               // Copy DTB to stack region.

    // Set X0 to StackBase, which is the new DTB address
    ldr     x0, _StackBase          // Store stack base address in x5.
    b       _LinuxStart             // Boot linux kernel.
    b      _Dead                    // We should never get here.

// Copy Sub program, X4 is src, X5 is dst, X6 is size
_CopyLoop:
    ldp     x7, x8, [x4], #0x10     // Save value at [x4](pointer) to x7 and x8, then x4 add 16.
    stp     x7, x8, [x5], #0x10     // Save value in x7 and x8 to [x5](pointer), then x5 add 16
    subs    x6, x6, #0x10           // x6 - 16, if , set CPSR register to 0.
    b.ne    _CopyLoop               // Check CPSR, if CPSR != 0, jump back to _CopyLoop.
    ret                             // Return when finish.

_Dead:
    b       _Dead                   // We should never get here.

.text
.align 4

_ShellCodeEnd:
/* Do not remove the last line  */

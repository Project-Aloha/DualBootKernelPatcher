/* DO NOT EDIT START */
// 16 instructions in total.
//
// Codes here will not be injected into the kernel, so they won't be run.
// This header is used to make things clearer.
// Size of this header must be 0x40 after compiling to binary.

_KernelHead:
    b       0x40        // Code 1, go to ShimLoader

_LinuxStart:
    b       0x0         // Code 2, go to Linux Kernel
    .ascii  "SHIM"      // ShimLoader magic
    .ascii  "LDR\0"
    .long   0           // Effective Image size L
    .long   0           // Effective Image size H
    .long   0           // Flags L
    .long   0           // Flags H

_CopyAddress:
_ManifestOffset:
    .long   0           // reserved2 LWORD, Manifest offset L
    .long   0           // reserved2 HWORD, Manifest offset H

_CopySize:
_ManifestSize:
    .long   0           // reserved3 LWORD, Manifest size L
    .long   0           // reserved3 HWORD, Manifest size H

_BaseImageSize:
    .long   0           // reserved4, Kernel Size L
    .long   0           // reserved4, Kernel Size H

    .ascii  "ARM\x64"   // magic_64
    .long   0           // reserve5

/* DO NOT EDIT END */

_ShimLoaderStart:
    adr     x4, _KernelHead
    ldr     x5, _ManifestOffset
    add     x5, x4, x5
    ldr     x6, _ManifestSize
    ldr     x7, _BaseImageSize
    add     x7, x4, x7
    br      x7

_ShimLoaderEnd:

// Simplified Shim Image header. The packer writes direct payload parameters
// here; these Shims do not parse the manifest blob.

_ShimHead:
    b       _ShimStart

    nop
    .ascii  "SHIMASM\0"
    .quad   _ShimImageEnd - _ShimHead
    .quad   0

_PayloadOffset:
    .quad   0

_CopyAddress:
    .quad   0

_CopySize:
    .quad   0

    .ascii  "ARM\x64"
    .long   0

/* Do not remove the last line  */

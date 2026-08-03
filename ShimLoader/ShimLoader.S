.include "DummyHead.S"

_ShimLoaderStart:
    adr     x4, _KernelHead
    ldr     x5, _ManifestOffset
    add     x5, x4, x5
    ldr     x6, _ManifestSize
    ldr     x7, _BaseImageSize
    add     x7, x4, x7
    br      x7

_ShimLoaderEnd:

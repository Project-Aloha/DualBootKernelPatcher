/** @file
 *  DualBoot Kernel Patcher Source File.
 *
 *  This Program will help you inject shell code into header of linux kernel.
 *
 *  It only supports several formats of kernel:
 *      1. Image file compile from source
 *      2. Qualcomm patched kernel.
 *      3. Image file kernel with efi stub.
 *
 *  Copyright (c) 2021-2025 The DuoWoa authors. All rights reserved.
 *  MIT License
 *
 */

#include "utils.h"

/**
 * The main function will check and read given files,
 * pass it to patch function, and write patched kernel to file.
 *
 * @param argc  argc is numbers of argues given in cmdline.
 * @param argv  argv is a array that contains all given values.
 *
 * @retval  -EINVAL Given files are not found or format not match.
 *
 */
int main(int argc, char *argv[]) {
    // Print hello message.
    printf("Project Aloha DualBoot Kernel Image Patcher v1.2.0.0\n");
    printf("Copyright (c) 2021-2025 The DuoWoA authors\n\n");
    if (argc != 6) {
        // Print usage if arg numbers not meet.
        printf("Usage: <Kernel Image to Patch> <UEFI FD Image> <Patched Kernel Image Destination> "
               "<Config File> <Shell Code File>\n");
        return -EINVAL;
    }

    // Get file paths.
    FileContent originImage = {.filePath = argv[1]};
    FileContent uefiImage = {.filePath = argv[2]};
    FileContent outputImage = {.filePath = argv[3]};
    FileContent config = {.filePath = argv[4]};
    FileContent shellCode = {.filePath = argv[5]};

    // Print processing msg.
    printf("Patching %s with %s and saving to %s...\n\n", originImage.filePath,
           uefiImage.filePath,
           outputImage.filePath);

    // Read buffer from old kernel.
    if (!get_file_size(&originImage))
        return -EINVAL;
    originImage.fileBuffer = malloc(originImage.fileSize);
    read_file_content(&originImage);

    // Read buffer from uefi image.
    if (!get_file_size(&uefiImage))
        return -EINVAL;
    uefiImage.fileBuffer = malloc(uefiImage.fileSize);
    read_file_content(&uefiImage);

    // Parse config file.
    Config stack = {0};
    if (parse_config(&config, &stack)) {
        printf("Error: Parse config failed\n");
        return -EINVAL;
    }

    // Get ShellCode buffer.
    if (!get_file_size(&shellCode))
        return -EINVAL;
    shellCode.fileBuffer = malloc(shellCode.fileSize);
    read_file_content(&shellCode);

    // Patch Kernel.
    void *buf = PatchKernel(&originImage, &uefiImage, &shellCode, &outputImage, &stack);

    // Output buffer to new kernel.
    if (outputImage.fileBuffer != NULL && buf != NULL) {
        write_file_content(&outputImage);
    } else {
        printf("Error Patching Kernel.\n");
        return -EINVAL;
    }

    // Free buffers we allocated.
    free(shellCode.fileBuffer);
    free(originImage.fileBuffer);
    free(uefiImage.fileBuffer);
    free(outputImage.fileBuffer);

    // Print end message.
    printf("Image successfully patched.\n");
    printf("Please check the patched kernel image at %s.\n", outputImage.filePath);
    return 0;
}


/**
 * Patch kernel based on given fileContents,
 *
 * @param[in] kernel    origin kernel fileContent
 * @param[in] uefi  uefi fd fileContent
 * @param[in] shellCode shell code binary
 * @param[in,out] patchedKernel patched kernel fileContent
 * @param[in] config config info read from config
 *
 * @return patched kernel buffer
 *
 */
uint8_t *PatchKernel(pFileContent kernel, pFileContent uefi, pFileContent shellCode,
                     pFileContent patchedKernel, pConfig config) {
    // Allocate output buffer
    patchedKernel->fileSize = kernel->fileSize + uefi->fileSize;
    patchedKernel->fileBuffer = malloc(patchedKernel->fileSize);

    // Copy two buffers into patchedBuffer.
    memcpy(patchedKernel->fileBuffer, kernel->fileBuffer, kernel->fileSize);
    memcpy(patchedKernel->fileBuffer + kernel->fileSize, uefi->fileBuffer, uefi->fileSize);

    // Check ShellCode binary magic.
    if (shellCode->fileSize > 0x40 && strcmp((char *) (shellCode->fileBuffer + 8), "SHLLCOD") != 0) {
        printf("Error: shell code binary format not recognize.\n");
        return NULL;
    }

    // Check if kernel has UNCOMPRESSED_IMG Header.
    char kernelHeader[0x11] = {0};
    uint8_t hasHeader = 0;
    memcpy(kernelHeader, kernel->fileBuffer, 0x10);
    if (strcmp(kernelHeader, "UNCOMPRESSED_IMG") == 0) {
        printf("Kernel has UNCOMPRESSED_IMG header.\n");
        kernel->fileSize -= 0x14;   // The magic is not part of kernel.
        hasHeader |= 0b1;
    }

    // Check if kernel size % 0x10 == 0, which will cause copy loop(in shellcode) issue.
    if (kernel->fileSize % 0x10) {
        printf("Align kernel size to 0x10.\n");

        // Calculate align padding
        uint8_t padding = 0x10 - (kernel->fileSize % 0x10);

        // New kernel size and output size
        kernel->fileSize += padding;
        patchedKernel->fileSize += padding;

        // Reallocate patched kernel.
        void *ptr = realloc(patchedKernel->fileBuffer, patchedKernel->fileSize);
        if (ptr == NULL) {
            printf("Failed to reallocate patched kernel buffer.");
            return NULL;
        } else patchedKernel->fileBuffer = ptr;

        // Copy uefi image to 0x10 align place and fill padding part to 0.
        if (hasHeader & 0b1) // Kernel has header, need to add 0x14 offset while copying.
        {
            memmove(patchedKernel->fileBuffer + kernel->fileSize + 0x14,
                    patchedKernel->fileBuffer + kernel->fileSize - padding + 0x14, uefi->fileSize);
            memset(patchedKernel->fileBuffer + kernel->fileSize - padding + 0x14, 0, padding);
        } else {
            memmove(patchedKernel->fileBuffer + kernel->fileSize,
                    patchedKernel->fileBuffer + kernel->fileSize - padding, uefi->fileSize);
            memset(patchedKernel->fileBuffer + kernel->fileSize - padding, 0, padding);
        }
    }

    // Kernel has uncompressed_img header,
    if (hasHeader & 1)
        // Move our pointer after UNCOMPRESSED_IMG header before further processing,
        patchedKernel->fileBuffer += 0x14;

    /* Reserved for feature use */
    // Check if kernel was already patched.
    //   if config in kernel was same with the config provided here,
    //   set kernel size to previous kernel size. (which skipped copying previously uefi fd)
    //    if (*(uint64_t *) (kernel->fileBuffer + 0x20) == config->StackBase &&
    //        *(uint64_t *) (kernel->fileBuffer + 0x28) == config->StackSize) {
    //        printf("Kernel was already patched previously!\n"
    //               "Re-patching with new UEFI...\n");
    //        kernel->fileSize = *(uint64_t *) (kernel->fileBuffer + 0x30);
    //        patchedKernel->fileSize = kernel->fileSize + uefi->fileSize; // Update output kernel size
    //        printf("Old kernel size %llx\n", *(uint64_t *) (kernel->fileBuffer + 0x30));
    //        hasHeader |= 0b10;
    //    }

    // Determine the loading offset of the kernel first,
    // we are either going to find a b instruction on the
    // first instruction or the second one. First is problematic,
    // second is fine.

    // 0x14 is AArch64 b opcode
    // If Code1 is jump instruction, the kernel is an original kernel or a patched kernel.
    // However, if Code2 is also jump instruction, the kernel should be a patched kernel.
    if (patchedKernel->fileBuffer[3] == 0x14 && patchedKernel->fileBuffer[7] != 0x14) {
        // For kernel without EFI stub, we have to move the jump kernel instruction forward to Code2.
        // We have a branch instruction first, we need to fix things a bit.

        // First start by getting the actual value of the branch addr offset.
        uint8_t offsetInstructionBuffer[] = {patchedKernel->fileBuffer[0], patchedKernel->fileBuffer[1],
                                             patchedKernel->fileBuffer[2], 0};
        uint32_t *offsetInstruction = (uint32_t *) offsetInstructionBuffer;

        // Now subtract 1 instruction because we'll move this to the second instruction of the kernel header.
        (*offsetInstruction)--;

        // Convert back into an actual value that is usable as a b instruction
        offsetInstructionBuffer[3] = 0x14; // Useless but just for our sanity :)

        // Now write the instruction back into the kernel (instr 2)
        patchedKernel->fileBuffer[4] = offsetInstructionBuffer[0];
        patchedKernel->fileBuffer[5] = offsetInstructionBuffer[1];
        patchedKernel->fileBuffer[6] = offsetInstructionBuffer[2];
        patchedKernel->fileBuffer[7] = 0x14;
    } else // Patched Kernel
    if (patchedKernel->fileBuffer[3] == 0x14 && patchedKernel->fileBuffer[7] == 0x14) {
        printf("Patched kernel detected, redo patch with new fd.\n");
        kernel->fileSize = *(uint64_t *) (kernel->fileBuffer + 0x30);
        patchedKernel->fileSize = kernel->fileSize + uefi->fileSize; // Update output kernel size
    } else // Kernel with EFI Stub
    if (*(uint16_t *) (patchedKernel->fileBuffer) == 0x5A4D && patchedKernel->fileBuffer[7] == 0x14) {
        // Check if kernel has EFI Stub, 4D5A, "MZ"
        // Note:
        //   The magic is also an arm64 instruction, and the second instruction jump to kernel.
        // We will only rewrite EFI header here. (This will break the kernel)
        // For kernel with EFI stub, we only need to patch the efi header.
        printf("Kernel has EFI header.\n");
    } else // Unknown stuff
    if (patchedKernel->fileBuffer[7] != 0x14) {
        // There is no branch instruction!
        printf("Error: Invalid Kernel Image. Branch instruction not found within first two instruction slots.\n"
               "Please Check the kernel you provided.\n");
        return NULL;
    }

    // Alright, our kernel image has a compatible branch instruction, let's start.

    // First, add the jump to our code on instr 1
    // This directly jump right after the kernel header (there's enough headroom here)
    patchedKernel->fileBuffer[0] = 0x10;
    patchedKernel->fileBuffer[1] = 0;
    patchedKernel->fileBuffer[2] = 0;
    patchedKernel->fileBuffer[3] = 0x14;

    // Now we need to fill in the config base of our firmware
    // Config Base: 0x00000000 9FC00000 (64 bit!)
    patchedKernel->fileBuffer[0x20] = config->StackBase >> 0 & 0xFF;
    patchedKernel->fileBuffer[0x21] = config->StackBase >> 8 & 0xFF;
    patchedKernel->fileBuffer[0x22] = config->StackBase >> 16 & 0xFF;
    patchedKernel->fileBuffer[0x23] = config->StackBase >> 24 & 0xFF;
    patchedKernel->fileBuffer[0x24] = config->StackBase >> 32 & 0xFF;
    patchedKernel->fileBuffer[0x25] = config->StackBase >> 40 & 0xFF;
    patchedKernel->fileBuffer[0x26] = config->StackBase >> 48 & 0xFF;
    patchedKernel->fileBuffer[0x27] = config->StackBase >> 56 & 0xFF;

    // Then we need to fill in the config size of our firmware
    // Config Base: 0x00000000 00300000 (64 bit!)
    patchedKernel->fileBuffer[0x28] = config->StackSize >> 0 & 0xFF;
    patchedKernel->fileBuffer[0x29] = config->StackSize >> 8 & 0xFF;
    patchedKernel->fileBuffer[0x2A] = config->StackSize >> 16 & 0xFF;
    patchedKernel->fileBuffer[0x2B] = config->StackSize >> 24 & 0xFF;
    patchedKernel->fileBuffer[0x2C] = config->StackSize >> 32 & 0xFF;
    patchedKernel->fileBuffer[0x2D] = config->StackSize >> 40 & 0xFF;
    patchedKernel->fileBuffer[0x2E] = config->StackSize >> 48 & 0xFF;
    patchedKernel->fileBuffer[0x2F] = config->StackSize >> 56 & 0xFF;

    // Finally, we add in the total kernel image size because we need to jump over!
    patchedKernel->fileBuffer[0x30] = kernel->fileSize >> 0 & 0xFF;
    patchedKernel->fileBuffer[0x31] = kernel->fileSize >> 8 & 0xFF;
    patchedKernel->fileBuffer[0x32] = kernel->fileSize >> 16 & 0xFF;
    patchedKernel->fileBuffer[0x33] = kernel->fileSize >> 24 & 0xFF;
    patchedKernel->fileBuffer[0x34] = kernel->fileSize >> 32 & 0xFF;
    patchedKernel->fileBuffer[0x35] = kernel->fileSize >> 40 & 0xFF;
    patchedKernel->fileBuffer[0x36] = kernel->fileSize >> 48 & 0xFF;
    patchedKernel->fileBuffer[0x37] = kernel->fileSize >> 56 & 0xFF;

    // Now our header is fully patched, let's add a tiny bit
    // of code as well to decide what to do.
    // Ignore 0x40 Dummy Header in shellCode->fileBuffer, so we add 0x40 offset after it.
    memcpy(patchedKernel->fileBuffer + 0x40, shellCode->fileBuffer + 0x40, shellCode->fileSize - 0x40);

    // Move back our pointer and recalculate kernel size in kernel
    // header if the patched kernel buffer has UNCOMPRESSED_IMG header.
    if (hasHeader & 0b1) {
        patchedKernel->fileBuffer -= 0x14;
        size_t newKernelSize = kernel->fileSize + uefi->fileSize;
        patchedKernel->fileBuffer[0x10] = newKernelSize >> 0 & 0xFF;
        patchedKernel->fileBuffer[0x11] = newKernelSize >> 8 & 0xFF;
        patchedKernel->fileBuffer[0x12] = newKernelSize >> 16 & 0xFF;
        patchedKernel->fileBuffer[0x13] = newKernelSize >> 24 & 0xFF;
    }

    // And that's it, the user now can append executable code right after the kernel,
    // and upon closing up the device said code will run at boot. Have fun!
    return patchedKernel->fileBuffer;
}

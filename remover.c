/** @file
 *  Patch Remover Source File.
 *
 *  This Program will help you revert patched instructions in a patched kernel.
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
 * Receive args and handle status.
 *
 * @param argc  argc is numbers of argues given in cmdline.
 * @param argv  argv is a array that contains all given values.
 * @return program status
 */
int main(int argc, char *argv[]) {
    printf("Project Aloha DualBoot Patch Remover v1.2.0.0\n");
    printf("Copyright (c) 2021-2024 The DuoWoA authors\n\n");
    if (argc != 3) {
        // Print usage if arg numbers not meet.
        printf("args: %d\n", argc);
        printf("Usage: <Input Patched Kernel> <Output Kernel>\n");
        return -EINVAL;
    }

    // Program status.
    int status = 0;

    // Get file paths.
    FileContent patchedImage = {.filePath = argv[1]};
    FileContent outputImage = {.filePath = argv[2]};

    // Read buffer from patched kernel.
    if (!get_file_size(&patchedImage)) {
        status = -EINVAL;
        goto free_and_exit;
    }
    patchedImage.fileBuffer = malloc(patchedImage.fileSize);
    read_file_content(&patchedImage);

    // Remove!
    Remove(&patchedImage, &outputImage);

    // Output buffer to new kernel.
    if (outputImage.fileBuffer != NULL) {
        write_file_content(&outputImage);
    } else {
        printf("Error Removing Patch.\n");
        status = -EINVAL;
        goto free_and_exit;
    }

    // Free buffers we allocated.
    free_and_exit:
    free(patchedImage.fileBuffer);
    free(outputImage.fileBuffer);

    // Everything goes well
    printf("Patch successfully removed.\n");
    printf("Please check the unpatched kernel image at %s.\n", outputImage.filePath);
    return status;
}

/**
 * Revert patch in kernel and return output file buffer.
 *
 * @param patchedKernel Input Patched kernel.
 * @param outputKernel Output Reverted kernel.
 * @return Reverted kernel buffer, NULL if error processing.
 */
uint8_t *Remove(pFileContent patchedKernel, pFileContent outputKernel) {
    // Get previous config from patched kernel.
    size_t originKernelSize = *(uint64_t *) (patchedKernel->fileBuffer + 0x30);

    // Check if kernel has UNCOMPRESSED_IMG Header.
    char kernelHeader[0x11] = {0};
    uint8_t hasHeader = 0;
    memcpy(kernelHeader, patchedKernel->fileBuffer, 0x10);
    if (strcmp(kernelHeader, "UNCOMPRESSED_IMG") == 0) {
        printf("Kernel has UNCOMPRESSED_IMG header.\n");
        originKernelSize = *(uint64_t *) (patchedKernel->fileBuffer + 0x30 + 0x14) + 0x14;
        hasHeader |= 0b1;
    }

    // Allocate output buffer
    outputKernel->fileSize = originKernelSize;
    outputKernel->fileBuffer = malloc(outputKernel->fileSize);

    // Copy new buffer into outputBuffer.
    memcpy(outputKernel->fileBuffer, patchedKernel->fileBuffer, outputKernel->fileSize);

    // After copying, jump over header
    if (hasHeader & 0b1)
        outputKernel->fileBuffer += 0x14;

    // Now check if it is a patched kernel
    if (outputKernel->fileBuffer[3] == 0x14 && outputKernel->fileBuffer[7] == 0x14) {
        printf("Patched kernel detected.");
        // Recover Code1 jump instruction, jump to linux kernel directly.
        *(uint32_t *) outputKernel->fileBuffer =
                ((*(uint32_t *) (outputKernel->fileBuffer + 4) & ~(0xFF << 24)) + 1) | (0x14 << 24);
        // Clean Code2
        *(uint32_t *) (outputKernel->fileBuffer + 4) = 0;
    } else {
        printf("Not an valid kernel.");
        return NULL;
    }

    // Move back our pointer and recalculate kernel size in kernel
    // header if the patched kernel buffer has UNCOMPRESSED_IMG header.
    if (hasHeader & 0b1) {
        outputKernel->fileBuffer -= 0x14;
        size_t newKernelSize = outputKernel->fileSize - 0x14;
        outputKernel->fileBuffer[0x10] = newKernelSize >> 0 & 0xFF;
        outputKernel->fileBuffer[0x11] = newKernelSize >> 8 & 0xFF;
        outputKernel->fileBuffer[0x12] = newKernelSize >> 16 & 0xFF;
        outputKernel->fileBuffer[0x13] = newKernelSize >> 24 & 0xFF;
    }
    return outputKernel->fileBuffer;
}
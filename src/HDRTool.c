/** @file
* Create a header or remove a header of a
* kernel for android boot v1.
*
*  Copyright (c) 2021-2025 The DuoWoa authors. All rights reserved.
*  MIT License
*
*/

#include "utils.h"

int main(
    int argc,
    char *argv[]
) {
    // Print hello world message
    printf("Project Aloha Kernel Image HDR Patcher v1.2.0.0\n");
    printf("Copyright (c) 2021-2025 The DuoWoA authors\n\n");

    // Check parameters and print help message
    if (argc != 3) {
        printf("Usage: <Input Kernel> <Output Kernel>\n");
        return -EINVAL;
    }

    // Check if file exist
    // Init Input file content
    FileContent kernelInput = {.filePath = argv[1]};
    if (!get_file_size(&kernelInput)) {
        printf("Error: Input kernel file not found or invalid.\n");
        return -EINVAL;
    }
    kernelInput.fileBuffer = malloc(kernelInput.fileSize + 0x14);
    read_file_content(&kernelInput);
    // add a 0x14 offset of the buffer
    memmove(kernelInput.fileBuffer + 0x14, kernelInput.fileBuffer, kernelInput.fileSize);

    // Init Output file content
    FileContent kernelOutput = {.filePath = argv[2]};
    kernelOutput.fileSize = kernelInput.fileSize;
    kernelOutput.fileBuffer = kernelInput.fileBuffer + 0x14;

    // OK now check if kernel has a header
    // if it is, then remove it
    // if it is not, then create a header
    // Header format:
    //      0x00-0x0F: "UNCOMPRESSED_IMG" (16 bytes)
    //      0x10-0x17: Kernel size (8 bytes, little-endian)
    if (strncmp((char *) kernelInput.fileBuffer, "UNCOMPRESSED_IMG", 0x10) == 0) {
        // Kernel has a header, remove it
        printf("Kernel has UNCOMPRESSED_IMG header, removing...\n");
        kernelOutput.fileBuffer += 0x14; // Move past the header
        kernelOutput.fileSize -= 0x14; // Reduce size by header size
    } else {
        // Kernel does not have a header, create one
        printf("Kernel does not have UNCOMPRESSED_IMG header, creating...\n");
        // Reallocate buffer to add header
        kernelOutput.fileBuffer -= 0x14;
        // Set header value
        memcpy(kernelOutput.fileBuffer, "UNCOMPRESSED_IMG", 0x10);
        kernelOutput.fileBuffer[0x10] = kernelOutput.fileSize >> 0 & 0xFF;
        kernelOutput.fileBuffer[0x11] = kernelOutput.fileSize >> 8 & 0xFF;
        kernelOutput.fileBuffer[0x12] = kernelOutput.fileSize >> 16 & 0xFF;
        kernelOutput.fileBuffer[0x13] = kernelOutput.fileSize >> 24 & 0xFF;
        kernelOutput.fileSize += 0x14;
    }

    // Save the output kernel
    if (write_file_content(&kernelOutput)) {
        printf("Error: Failed to write output kernel file.\n");
        free(kernelInput.fileBuffer);
        return -EINVAL;
    }

    // Free allocated memory
    free(kernelInput.fileBuffer);

    // Print success message
    printf("Kernel image processed successfully.\n");
}

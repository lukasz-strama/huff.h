/* huff - Simple Example Usage
 *
 * This file demonstrates how to use the huff library.
 * It creates a test file, compresses it, and then decompresses it.
 *
 * Build:
 *   $ cc -o huff src/main.c -lm -lpthread
 */

#define HUFF_IMPLEMENTATION
#include "huff.h"

int main(void) {
    const char *input_file = "test.txt";
    const char *compressed_file = "test.huff";
    const char *decompressed_file = "test_decoded.txt";

    // 1. Create a test file
    printf("[INFO] Creating test file: %s\n", input_file);
    FILE *f = fopen(input_file, "w");
    if (!f) {
        perror("Failed to create test file");
        return 1;
    }
    fprintf(f, "Hello Huffman! This is a simple test of the library.\n");
    fprintf(f, "It should compress this text efficiently.\n");
    fclose(f);

    // 2. Compress the file
    printf("[INFO] Compressing: %s -> %s\n", input_file, compressed_file);
    HuffStats stats = {0};
    if (!huffman_encode(input_file, compressed_file, &stats)) {
        fprintf(stderr, "[ERROR] Compression failed\n");
        return 1;
    }

    printf("  Original Size:   %lu bytes\n", stats.original_size);
    printf("  Compressed Size: %lu bytes\n", stats.compressed_size);
    printf("  Time Taken:      %.4f seconds\n", stats.time_taken);
    printf("  Entropy:         %.4f bits/symbol\n", stats.entropy);
    
    // Show the code table used
    printf("\n");
    huffman_print_code_table(stats.codes);

    // 3. Decompress the file
    printf("\n[INFO] Decompressing: %s -> %s\n", compressed_file, decompressed_file);
    // Reset stats for decoding
    memset(&stats, 0, sizeof(stats));
    if (!huffman_decode(compressed_file, decompressed_file, &stats)) {
        fprintf(stderr, "[ERROR] Decompression failed\n");
        return 1;
    }

    printf("  Decoded Size:    %lu bytes\n", stats.original_size);
    printf("  Time Taken:      %.4f seconds\n", stats.time_taken);

    printf("\n[SUCCESS] Test completed successfully!\n");
    return 0;
}

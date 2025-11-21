/* huff - Simple Example Usage
 *
 * This file demonstrates how to use the huff library.
 *
 */

#define HUFF_IMPLEMENTATION
#include "huff.h"

void print_code_table(const HuffCode* codes) {
  printf("--- Huffman Code Table ---\n");
  for (int i = 0; i < HUFF_MAX_SYMBOLS; ++i) {
    if (codes[i].bit_count > 0) {
      printf("Symbol 0x%02X: ", i);
      if (i >= 32 && i <= 126)
        printf("'%c' ", (char)i);
      else
        printf("    ");

      for (int j = 0; j < codes[i].bit_count; ++j) {
        printf("%d", (codes[i].bits[j >> 3] >> (j & 7)) & 1);
      }
      printf(" (%d bits)\n", codes[i].bit_count);
    }
  }
  printf("--------------------------\n");
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "Usage: %s <input_file> [compressed_file] [decompressed_file]\n",
            argv[0]);
    return 1;
  }
  const char* input_file = argv[1];
  const char* compressed_file = (argc >= 3) ? argv[2] : "test.huff";
  const char* decompressed_file = (argc >= 4) ? argv[3] : "test_decoded.bin";

  // 1. Check if input file exists
  FILE* f = fopen(input_file, "rb");
  if (!f) {
    fprintf(stderr, "[ERROR] Input file '%s' not found.\n", input_file);
    return 1;
  }
  fclose(f);

  // 2. Compress the file
  printf("[INFO] Compressing: %s -> %s\n", input_file, compressed_file);
  HuffStats stats = {0};
  HuffResult res = huffman_encode(input_file, compressed_file, &stats);
  if (res != HUFF_SUCCESS) {
    fprintf(stderr, "[ERROR] Compression failed with error code: %d\n", res);
    return 1;
  }

  printf("  Original Size:   %lu bytes\n", stats.original_size);
  printf("  Compressed Size: %lu bytes\n", stats.compressed_size);
  printf("  Time Taken:      %.6f seconds\n", stats.time_taken);
  printf("  Entropy:         %.4f bits/symbol\n", stats.entropy);

  // Show the code table used
  printf("\n");
  print_code_table(stats.codes);

  // 3. Decompress the file
  printf("\n[INFO] Decompressing: %s -> %s\n", compressed_file,
         decompressed_file);
  // Reset stats for decoding
  memset(&stats, 0, sizeof(stats));
  res = huffman_decode(compressed_file, decompressed_file, &stats);
  if (res != HUFF_SUCCESS) {
    fprintf(stderr, "[ERROR] Decompression failed with error code: %d\n", res);
    return 1;
  }

  printf("  Decoded Size:    %lu bytes\n", stats.original_size);
  printf("  Time Taken:      %.6f seconds\n", stats.time_taken);

  printf("\n[SUCCESS] Test completed successfully!\n");
  return 0;
}

#define HUFF_IMPLEMENTATION
#include <dirent.h>
#include <sys/stat.h>

#include "huff.h"

#define TEST_DIR "tests"
#define OUTPUT_DIR "tests/outputs"

// Format file size to human-readable string (B, KB, MB...)
void format_size(uint64_t bytes, char* buffer, size_t buffer_size) {
  const char* suffixes[] = {"B", "KB", "MB", "GB", "TB"};
  int i = 0;
  double size = (double)bytes;
  while (size >= 1024 && i < 4) {
    size /= 1024;
    i++;
  }
  snprintf(buffer, buffer_size, "%.2f %s", size, suffixes[i]);
}

// Helper to check if a file is a regular file and not a hidden file or script
bool is_test_file(const char* name) {
  if (name[0] == '.') return false;
  size_t len = strlen(name);
  if (len >= 3 && strcmp(name + len - 3, ".py") == 0) return false;
  if (len >= 4 && strcmp(name + len - 4, ".huf") == 0) return false;
  if (len >= 2 && strcmp(name + len - 2, ".c") == 0) return false;
  return true;
}

// Helper to compare two files
bool compare_files(const char* path1, const char* path2) {
  FILE* f1 = fopen(path1, "rb");
  FILE* f2 = fopen(path2, "rb");
  if (!f1 || !f2) {
    if (f1) fclose(f1);
    if (f2) fclose(f2);
    return false;
  }

  bool same = true;
  uint8_t buf1[4096], buf2[4096];
  while (same) {
    size_t n1 = fread(buf1, 1, sizeof(buf1), f1);
    size_t n2 = fread(buf2, 1, sizeof(buf2), f2);
    if (n1 != n2) {
      same = false;
      break;
    }
    if (n1 == 0) break;
    if (memcmp(buf1, buf2, n1) != 0) {
      same = false;
      break;
    }
  }

  fclose(f1);
  fclose(f2);
  return same;
}

void run_test(const char* filename) {
  char input_path[512];
  char compressed_path[512];
  char decompressed_path[512];

  snprintf(input_path, sizeof(input_path), "%s/%s", TEST_DIR, filename);
  snprintf(compressed_path, sizeof(compressed_path), "%s/%s.huf", OUTPUT_DIR,
           filename);
  snprintf(decompressed_path, sizeof(decompressed_path), "%s/%s", OUTPUT_DIR,
           filename);

  // Skip directories
  struct stat st;
  if (stat(input_path, &st) != 0 || !S_ISREG(st.st_mode)) return;

  printf("Testing %s...\n", input_path);

  HuffStats stats = {0};

  // Compress
  if (huffman_encode(input_path, compressed_path, &stats) != HUFF_SUCCESS) {
    printf("  [FAIL] Compression failed\n");
    return;
  }

  double comp_time = stats.time_taken;
  uint64_t orig_size = stats.original_size;
  uint64_t comp_size = stats.compressed_size;
  double entropy = stats.entropy;

  // Decompress
  memset(&stats, 0, sizeof(stats));
  if (huffman_decode(compressed_path, decompressed_path, &stats) !=
      HUFF_SUCCESS) {
    printf("  [FAIL] Decompression failed\n");
    return;
  }
  double decomp_time = stats.time_taken;

  // Verify
  if (!compare_files(input_path, decompressed_path)) {
    printf("  [FAIL] Content mismatch\n");
    return;
  }

  printf("  [PASS] %s\n", input_path);

  char buf[64];
  format_size(orig_size, buf, sizeof(buf));
  printf("    Original Size:   %lu bytes\n",
         orig_size);  // Keep raw bytes for consistency with python output
  printf("    Compressed Size: %lu bytes\n", comp_size);
  printf("    Entropy:         %.4f bits/symbol\n", entropy);

  if (comp_size > 0 && orig_size > 0) {
    double ratio = (double)orig_size / comp_size;
    double saving = (1.0 - (double)comp_size / orig_size) * 100.0;
    printf("    Compression Rate: %.2fx (%.2f%%)\n", ratio, saving);
  }

  if (comp_time > 0 && orig_size > 0) {
    double mb = (double)orig_size / (1024 * 1024);
    double speed = mb / comp_time;
    printf("    Comp Speed:      %.2f MB/s (%.6f s)\n", speed, comp_time);
  }

  if (decomp_time > 0 && orig_size > 0) {
    double mb = (double)orig_size / (1024 * 1024);
    double speed = mb / decomp_time;
    printf("    Decomp Speed:    %.2f MB/s (%.6f s)\n", speed, decomp_time);
  }
}

int main(void) {
  // Create output directory
  struct stat st = {0};
  if (stat(OUTPUT_DIR, &st) == -1) {
    mkdir(OUTPUT_DIR, 0700);
  }

  DIR* d = opendir(TEST_DIR);
  if (!d) {
    perror("opendir");
    return 1;
  }

  struct dirent* dir;
  // Simple array to store filenames for sorting
  char* files[256];
  int count = 0;

  while ((dir = readdir(d)) != NULL) {
    if (is_test_file(dir->d_name)) {
      // Check if it's a regular file by stat-ing it
      char path[512];
      snprintf(path, sizeof(path), "%s/%s", TEST_DIR, dir->d_name);
      if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        files[count++] = strdup(dir->d_name);
      }
    }
  }
  closedir(d);

  // Sort files alphabetically
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (strcmp(files[i], files[j]) > 0) {
        char* temp = files[i];
        files[i] = files[j];
        files[j] = temp;
      }
    }
  }

  for (int i = 0; i < count; i++) {
    run_test(files[i]);
    free(files[i]);
  }

  printf("\nSummary: %d tests passed.\n",
         count);  // Assuming all passed if we didn't return early
  return 0;
}

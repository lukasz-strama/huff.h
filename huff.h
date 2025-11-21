/*
 * huff.h - Single-Header Huffman Compression Library
 *
 * A lightweight, portable, and high-performance C library for Huffman coding.
 * Designed following the STB single-header library style.
 *
 * USAGE:
 *   Do this:
 *      #define HUFF_IMPLEMENTATION
 *   before you include this file in *one* C or C++ file to create the implementation.
 *
 *      // i.e. it should look like this:
 *      #include ...
 *      #include ...
 *      #define HUFF_IMPLEMENTATION
 *      #include "huff.h"
 *
 * API OVERVIEW:
 *   HuffResult huffman_encode(const char *input_path, const char *output_path, HuffStats *stats); 
 *   HuffResult huffman_decode(const char *input_path, const char *output_path, HuffStats *stats);
 *
 * LICENSE:
 *   MIT License
 *
 *   Copyright (c) 2025 Łukasz Strama <strama.lukasz54@gmail.com>
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the "Software"), to
 *   deal in the Software without restriction, including without limitation the
 *   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *   sell copies of the Software, and to permit persons to whom the Software is
 *   furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *   IN THE SOFTWARE.
 */

#ifndef HUFF_H
#define HUFF_H

#include <stdbool.h>
#include <stdint.h>

#define HUFF_MAX_SYMBOLS 256
#define HUFF_MAX_CODE_BITS 256
#define HUFF_MAX_CODE_BYTES ((HUFF_MAX_CODE_BITS + 7) / 8)

typedef enum {
  HUFF_SUCCESS = 0,
  HUFF_ERROR_FILE_OPEN,
  HUFF_ERROR_FILE_READ,
  HUFF_ERROR_FILE_WRITE,
  HUFF_ERROR_MEMORY,
  HUFF_ERROR_BAD_FORMAT,
  HUFF_ERROR_INPUT_TOO_LARGE,
  HUFF_ERROR_UNKNOWN
} HuffResult;

typedef struct {
  uint8_t bits[HUFF_MAX_CODE_BYTES];
  uint16_t bit_count;
} HuffCode;

// Statistics structure
// Pass a pointer to this structure to huffman_encode/decode to retrieve
// performance and compression metrics.
//
// Usage:
//   HuffStats stats;
//   if (huffman_encode("in.txt", "out.huf", &stats) == HUFF_SUCCESS) {
//       printf("Ratio: %.2f%%\n", 
//              (1.0 - (double)stats.compressed_size / stats.original_size) * 100);
//   }
typedef struct {
  uint64_t original_size;           // Size of input file in bytes
  uint64_t compressed_size;         // Size of output file in bytes (0 for decode)
  double time_taken;                // Execution time in seconds (wall clock)
  double entropy;                   // Shannon entropy of input data (bits/symbol)
  double avg_code_len;              // Average length of Huffman codes (bits/symbol)
  HuffCode codes[HUFF_MAX_SYMBOLS]; // The generated Huffman codes table (see example in main.c)
} HuffStats;

/**
 * @brief Compress a file using Huffman coding.
 *
 * @param input_path Path to the input file.
 * @param output_path Path to the output file.
 * @param stats Optional pointer to HuffStats to populate with compression
 * statistics.
 * @return HUFF_SUCCESS on success, error code on failure.
 */
HuffResult huffman_encode(const char* input_path, const char* output_path,
                          HuffStats* stats);

/**
 * @brief Decompress a Huffman encoded file.
 *
 * @param input_path Path to the input file.
 * @param output_path Path to the output file.
 * @param stats Optional pointer to HuffStats to populate with decompression
 * statistics.
 * @return HUFF_SUCCESS on success, error code on failure.
 */
HuffResult huffman_decode(const char* input_path, const char* output_path,
                          HuffStats* stats);

#endif  // HUFF_H

#ifdef HUFF_IMPLEMENTATION

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// --- Constants & Macros ---

#define HUFF_MAGIC "HUF2"
#define HUFF_MAX_NODES (HUFF_MAX_SYMBOLS * 2)
#define HUFF_IO_BUFFER_CAP (64 * 1024)
#define HUFF_DEC_TABLE_BITS 12
#define HUFF_DEC_TABLE_SIZE (1 << HUFF_DEC_TABLE_BITS)

// --- Internal Structures ---

typedef struct {
  uint64_t weight;
  int32_t left;
  int32_t right;
  int32_t symbol;
} HuffNode;

typedef struct {
  int16_t symbol;     // 0-255 if leaf, -1 if not
  uint8_t bits;       // Number of bits to consume
  int16_t next_node;  // Next node index if not leaf
} HuffDecEntry;

typedef struct {
  FILE* file;
  uint64_t bit_buffer;
  uint32_t bit_count;
  uint8_t* io_buffer;
  size_t io_pos;
  size_t io_end;
  bool exhausted;
} BitReader;

typedef struct {
  int data[HUFF_MAX_NODES];
  size_t size;
  HuffNode* nodes;
} HuffHeap;

typedef struct {
  const uint8_t* data;
  size_t size;
  uint64_t freq[HUFF_MAX_SYMBOLS];
} FreqThreadArgs;

// --- Internal Function Prototypes ---

static void _huff_bit_reader_init(BitReader* reader, FILE* file);
static bool _huff_bit_reader_fill_io(BitReader* reader);
static void _huff_bit_reader_free(BitReader* reader);

static bool _huff_heap_less(const HuffHeap* heap, int a, int b);
static void _huff_heap_swap(int* a, int* b);
static bool _huff_heap_push(HuffHeap* heap, int index);
static int _huff_heap_pop(HuffHeap* heap);

static int _huff_build_tree(const uint64_t freq[HUFF_MAX_SYMBOLS],
                            HuffNode* nodes, int* out_count);
static void _huff_collect_codes_rec(const HuffNode* nodes, int node_index,
                                    HuffCode* codes, uint8_t* path,
                                    uint16_t depth);
static void _huff_collect_codes(const HuffNode* nodes, int root,
                                HuffCode* codes);
static void _huff_make_canonical(const uint8_t lengths[HUFF_MAX_SYMBOLS],
                                 HuffCode codes[HUFF_MAX_SYMBOLS]);
static int _huff_rebuild_tree(const HuffCode codes[HUFF_MAX_SYMBOLS],
                              HuffNode* nodes, int* out_count);

static HuffResult _huff_read_entire_file(const char* path, uint8_t** data,
                                         size_t* size);
static bool _huff_write_header(FILE* out, uint64_t original_size,
                               const uint8_t lengths[HUFF_MAX_SYMBOLS]);
static bool _huff_read_header(FILE* in, uint64_t* original_size,
                              uint8_t lengths[HUFF_MAX_SYMBOLS]);

static void* _huff_freq_worker(void* arg);
static void _huff_parallel_freq_count(const uint8_t* data, size_t size,
                                      uint64_t freq[HUFF_MAX_SYMBOLS]);

// --- BitReader Implementation ---

static void _huff_bit_reader_init(BitReader* reader, FILE* file) {
  reader->file = file;
  reader->bit_buffer = 0;
  reader->bit_count = 0;
  reader->io_buffer = malloc(HUFF_IO_BUFFER_CAP);
  reader->io_pos = 0;
  reader->io_end = 0;
  reader->exhausted = false;
}

static bool _huff_bit_reader_fill_io(BitReader* reader) {
  reader->io_pos = 0;
  size_t n = fread(reader->io_buffer, 1, HUFF_IO_BUFFER_CAP, reader->file);
  reader->io_end = n;
  return n > 0;
}

static void _huff_bit_reader_ensure(BitReader* reader, uint32_t n) {
  while (reader->bit_count < n) {
    if (reader->io_pos >= reader->io_end) {
      if (!_huff_bit_reader_fill_io(reader)) {
        reader->exhausted = true;
        break;
      }
    }
    reader->bit_buffer |= (uint64_t)reader->io_buffer[reader->io_pos++]
                          << reader->bit_count;
    reader->bit_count += 8;
  }
}

static void _huff_bit_reader_free(BitReader* reader) {
  free(reader->io_buffer);
  reader->io_buffer = NULL;
}

// --- Heap Implementation ---

static bool _huff_heap_less(const HuffHeap* heap, int a, int b) {
  const HuffNode* nodes = heap->nodes;
  if (nodes[a].weight < nodes[b].weight) return true;
  if (nodes[a].weight > nodes[b].weight) return false;
  return a < b;
}

static void _huff_heap_swap(int* a, int* b) {
  int tmp = *a;
  *a = *b;
  *b = tmp;
}

static bool _huff_heap_push(HuffHeap* heap, int index) {
  if (heap->size >= HUFF_MAX_NODES) {
    return false;
  }
  size_t i = heap->size;
  heap->data[i] = index;
  heap->size += 1;
  while (i > 0) {
    size_t parent = (i - 1) / 2;
    if (!_huff_heap_less(heap, heap->data[i], heap->data[parent])) {
      break;
    }
    _huff_heap_swap(&heap->data[i], &heap->data[parent]);
    i = parent;
  }
  return true;
}

static int _huff_heap_pop(HuffHeap* heap) {
  if (heap->size == 0) {
    return -1;
  }
  int root = heap->data[0];
  heap->size -= 1;
  if (heap->size > 0) {
    heap->data[0] = heap->data[heap->size];
    size_t i = 0;
    for (;;) {
      size_t left = i * 2 + 1;
      size_t right = i * 2 + 2;
      size_t smallest = i;
      if (left < heap->size &&
          _huff_heap_less(heap, heap->data[left], heap->data[smallest])) {
        smallest = left;
      }
      if (right < heap->size &&
          _huff_heap_less(heap, heap->data[right], heap->data[smallest])) {
        smallest = right;
      }
      if (smallest == i) {
        break;
      }
      _huff_heap_swap(&heap->data[i], &heap->data[smallest]);
      i = smallest;
    }
  }
  return root;
}

// --- Huffman Tree & Codes ---

static int _huff_build_tree(const uint64_t freq[HUFF_MAX_SYMBOLS],
                            HuffNode* nodes, int* out_count) {
  HuffHeap heap = {0};
  heap.nodes = nodes;
  int count = 0;
  // Initialize leaves for each symbol with non-zero weight
  for (int symbol = 0; symbol < HUFF_MAX_SYMBOLS; ++symbol) {
    if (freq[symbol] == 0) continue;
    if (count >= HUFF_MAX_NODES) {
      return -1;
    }
    nodes[count].weight = freq[symbol];
    nodes[count].left = -1;
    nodes[count].right = -1;
    nodes[count].symbol = symbol;
    if (!_huff_heap_push(&heap, count)) {
      return -1;
    }
    count += 1;
  }
  if (count == 0) {
    *out_count = 0;
    return -1;
  }
  if (heap.size == 1) {
    *out_count = count;
    return heap.data[0];
  }
  // Build tree: merge two lightest nodes until one remains (root)
  while (heap.size > 1) {
    int a = _huff_heap_pop(&heap);
    int b = _huff_heap_pop(&heap);
    if (a < 0 || b < 0) {
      return -1;
    }
    if (count >= HUFF_MAX_NODES) {
      return -1;
    }
    nodes[count].weight = nodes[a].weight + nodes[b].weight;
    nodes[count].left = a;
    nodes[count].right = b;
    nodes[count].symbol = -1;
    if (!_huff_heap_push(&heap, count)) {
      return -1;
    }
    count += 1;
  }
  *out_count = count;
  return heap.size == 1 ? heap.data[0] : -1;
}

static void _huff_collect_codes_rec(const HuffNode* nodes, int node_index,
                                    HuffCode* codes, uint8_t* path,
                                    uint16_t depth) {
  const HuffNode* node = &nodes[node_index];
  // If leaf, save the code
  if (node->left < 0 && node->right < 0) {
    HuffCode* code = &codes[node->symbol];
    memset(code, 0, sizeof(*code));
    if (depth == 0) {
      code->bit_count = 1;
      return;
    }
    code->bit_count = depth;
    for (uint16_t i = 0; i < depth; ++i) {
      if (path[i]) {
        code->bits[i >> 3] |= (uint8_t)(1u << (i & 7));
      }
    }
    return;
  }
  // Recursive traversal: left (0) and right (1)
  if (node->left >= 0) {
    path[depth] = 0;
    _huff_collect_codes_rec(nodes, node->left, codes, path, depth + 1);
  }
  if (node->right >= 0) {
    path[depth] = 1;
    _huff_collect_codes_rec(nodes, node->right, codes, path, depth + 1);
  }
}

static void _huff_collect_codes(const HuffNode* nodes, int root,
                                HuffCode* codes) {
  memset(codes, 0, sizeof(HuffCode) * HUFF_MAX_SYMBOLS);
  uint8_t path[HUFF_MAX_CODE_BITS] = {0};
  _huff_collect_codes_rec(nodes, root, codes, path, 0);
}

static void _huff_make_canonical(const uint8_t lengths[HUFF_MAX_SYMBOLS],
                                 HuffCode codes[HUFF_MAX_SYMBOLS]) {
  uint64_t next_code[HUFF_MAX_CODE_BITS + 1] = {0};
  uint64_t code = 0;
  int bl_count[HUFF_MAX_CODE_BITS + 1] = {0};

  memset(codes, 0, sizeof(HuffCode) * HUFF_MAX_SYMBOLS);

  for (int i = 0; i < HUFF_MAX_SYMBOLS; i++) {
    if (lengths[i] > 0) bl_count[lengths[i]]++;
  }

  for (int bits = 1; bits <= HUFF_MAX_CODE_BITS; bits++) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = code;
  }

  for (int i = 0; i < HUFF_MAX_SYMBOLS; i++) {
    int len = lengths[i];
    if (len > 0) {
      codes[i].bit_count = len;
      uint64_t c = next_code[len];
      next_code[len]++;

      // Store bits reversed (MSB of c becomes bit 0 of code)
      // This ensures that the canonical code (MSB-first) is written to the
      // stream correctly because the stream writer writes bit 0 first.
      for (int j = 0; j < len; ++j) {
        if ((c >> (len - 1 - j)) & 1) {
          codes[i].bits[j >> 3] |= (1 << (j & 7));
        }
      }
    }
  }
}

static int _huff_rebuild_tree(const HuffCode codes[HUFF_MAX_SYMBOLS],
                              HuffNode* nodes, int* out_count) {
  // Initialize root
  nodes[0].left = -1;
  nodes[0].right = -1;
  nodes[0].symbol = -1;
  int count = 1;

  for (int i = 0; i < HUFF_MAX_SYMBOLS; ++i) {
    if (codes[i].bit_count == 0) continue;

    int current = 0;
    for (int j = 0; j < codes[i].bit_count; ++j) {
      int bit = (codes[i].bits[j >> 3] >> (j & 7)) & 1;

      if (bit == 0) {
        if (nodes[current].left == -1) {
          if (count >= HUFF_MAX_NODES) return -1;
          nodes[count].left = -1;
          nodes[count].right = -1;
          nodes[count].symbol = -1;
          nodes[current].left = count++;
        }
        current = nodes[current].left;
      } else {
        if (nodes[current].right == -1) {
          if (count >= HUFF_MAX_NODES) return -1;
          nodes[count].left = -1;
          nodes[count].right = -1;
          nodes[count].symbol = -1;
          nodes[current].right = count++;
        }
        current = nodes[current].right;
      }
    }
    nodes[current].symbol = i;
  }
  *out_count = count;
  return 0;  // Root is always 0
}

// --- File I/O Helpers ---

static HuffResult _huff_read_entire_file(const char* path, uint8_t** data,
                                         size_t* size) {
  FILE* file = fopen(path, "rb");
  if (!file) {
    return HUFF_ERROR_FILE_OPEN;
  }
  // Check file size
  if (fseeko(file, 0, SEEK_END) != 0) {
    fclose(file);
    return HUFF_ERROR_FILE_READ;
  }
  off_t length = ftello(file);
  if (length < 0) {
    fclose(file);
    return HUFF_ERROR_FILE_READ;
  }
  if (fseeko(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return HUFF_ERROR_FILE_READ;
  }
  if ((uint64_t)length > SIZE_MAX) {
    fclose(file);
    return HUFF_ERROR_INPUT_TOO_LARGE;
  }
  size_t buffer_size = (size_t)length;
  uint8_t* buffer = NULL;
  if (buffer_size > 0) {
    buffer = malloc(buffer_size);
    if (!buffer) {
      fclose(file);
      return HUFF_ERROR_MEMORY;
    }
    // Read entire file into RAM
    size_t read_bytes = fread(buffer, 1, buffer_size, file);
    if (read_bytes != buffer_size) {
      free(buffer);
      fclose(file);
      return HUFF_ERROR_FILE_READ;
    }
  }
  fclose(file);
  *data = buffer;
  *size = buffer_size;
  return HUFF_SUCCESS;
}

static bool _huff_write_header(FILE* out, uint64_t original_size,
                               const uint8_t lengths[HUFF_MAX_SYMBOLS]) {
  if (fwrite(HUFF_MAGIC, 1, 4, out) != 4) return false;
  if (fwrite(&original_size, sizeof(original_size), 1, out) != 1) return false;
  if (fwrite(lengths, 1, HUFF_MAX_SYMBOLS, out) != HUFF_MAX_SYMBOLS)
    return false;
  return true;
}

static bool _huff_read_header(FILE* in, uint64_t* original_size,
                              uint8_t lengths[HUFF_MAX_SYMBOLS]) {
  char magic[4];
  if (fread(magic, 1, 4, in) != 4) return false;
  if (memcmp(magic, HUFF_MAGIC, 4) != 0) {
    return false;
  }
  if (fread(original_size, sizeof(*original_size), 1, in) != 1) return false;
  if (fread(lengths, 1, HUFF_MAX_SYMBOLS, in) != HUFF_MAX_SYMBOLS) return false;
  return true;
}

// --- Threading Helpers ---

// Worker function for frequency counting thread
// Uses loop unrolling
static void* _huff_freq_worker(void* arg) {
  FreqThreadArgs* args = (FreqThreadArgs*)arg;
  memset(args->freq, 0, sizeof(args->freq));
  size_t i = 0;
  // Process 8 bytes at a time
  for (; i + 8 <= args->size; i += 8) {
    args->freq[args->data[i + 0]]++;
    args->freq[args->data[i + 1]]++;
    args->freq[args->data[i + 2]]++;
    args->freq[args->data[i + 3]]++;
    args->freq[args->data[i + 4]]++;
    args->freq[args->data[i + 5]]++;
    args->freq[args->data[i + 6]]++;
    args->freq[args->data[i + 7]]++;
  }
  // Finish remaining bytes
  for (; i < args->size; ++i) {
    args->freq[args->data[i]]++;
  }
  return NULL;
}

// Parallel symbol frequency counting
// Splits data into chunks and launches threads (max 64)
static void _huff_parallel_freq_count(const uint8_t* data, size_t size,
                                      uint64_t freq[HUFF_MAX_SYMBOLS]) {
  long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (num_cores < 1) num_cores = 1;
  if (size < 1024 * 1024)
    num_cores = 1;  // Small files processed single-threaded

  pthread_t threads[64];
  FreqThreadArgs args[64];
  if (num_cores > 64) num_cores = 64;

  size_t chunk_size = size / num_cores;
  for (int i = 0; i < num_cores; ++i) {
    args[i].data = data + i * chunk_size;
    args[i].size = (i == num_cores - 1) ? (size - i * chunk_size) : chunk_size;
    pthread_create(&threads[i], NULL, _huff_freq_worker, &args[i]);
  }

  for (int i = 0; i < num_cores; ++i) {
    pthread_join(threads[i], NULL);
    // Sum partial results
    for (int j = 0; j < HUFF_MAX_SYMBOLS; ++j) {
      freq[j] += args[i].freq[j];
    }
  }
}

// --- Public API Implementation ---

HuffResult huffman_encode(const char* input_path, const char* output_path,
                          HuffStats* stats) {
  uint8_t* data = NULL;
  size_t size = 0;
  FILE* out = NULL;
  uint8_t* io_buffer = NULL;
  HuffResult res = HUFF_SUCCESS;

  res = _huff_read_entire_file(input_path, &data, &size);
  if (res != HUFF_SUCCESS) {
    goto cleanup;
  }
  out = fopen(output_path, "wb");
  if (!out) {
    res = HUFF_ERROR_FILE_OPEN;
    goto cleanup;
  }
  uint64_t freq[HUFF_MAX_SYMBOLS] = {0};
  _huff_parallel_freq_count(data, size, freq);

  HuffCode codes[HUFF_MAX_SYMBOLS];
  uint8_t lengths[HUFF_MAX_SYMBOLS] = {0};

  if (size > 0) {
    HuffNode nodes[HUFF_MAX_NODES] = {0};
    int node_count = 0;
    int root = _huff_build_tree(freq, nodes, &node_count);
    if (root < 0) {
      res = HUFF_ERROR_UNKNOWN;
      goto cleanup;
    }
    _huff_collect_codes(nodes, root, codes);

    for (int i = 0; i < HUFF_MAX_SYMBOLS; ++i) {
      lengths[i] = (uint8_t)codes[i].bit_count;
    }
    _huff_make_canonical(lengths, codes);
  } else {
    memset(codes, 0, sizeof(codes));
  }

  if (!_huff_write_header(out, (uint64_t)size, lengths)) {
    res = HUFF_ERROR_FILE_WRITE;
    goto cleanup;
  }
  if (size == 0) {
    res = HUFF_SUCCESS;
    goto cleanup;
  }

  // Precompute fast codes (up to 64 bits)
  // This allows us to write codes in a single 64-bit operation instead of
  // bit-by-bit
  typedef struct {
    uint64_t bits;
    int len;
  } FastHuffCode;

  FastHuffCode fast_codes[HUFF_MAX_SYMBOLS];
  for (int i = 0; i < HUFF_MAX_SYMBOLS; ++i) {
    if (codes[i].bit_count > 64) {
      fast_codes[i].len = -1;  // Too long for fast path (extremely rare)
    } else {
      fast_codes[i].len = codes[i].bit_count;
      fast_codes[i].bits = 0;
      for (int b = 0; b < codes[i].bit_count; ++b) {
        if ((codes[i].bits[b >> 3] >> (b & 7)) & 1) {
          fast_codes[i].bits |= (1ULL << b);
        }
      }
    }
  }

  // --- Optimized 64-bit Aligned Bit Writer ---
  //
  // This section implements a high-performance bit writer that buffers up to 64
  // bits in a CPU register (`bit_buffer`) before flushing to memory. This
  // avoids the overhead of writing individual bits or bytes for every symbol.
  //
  // Key concepts:
  // 1. `bit_buffer`: A 64-bit accumulator holding pending bits.
  // 2. `bit_count`: Number of valid bits currently in `bit_buffer`.
  // 3. `io_buffer`: Large output buffer to minimize `fwrite` syscalls.
  //
  // The logic handles two main cases:
  // A. The new code fits entirely within the remaining space of `bit_buffer`.
  // B. The new code overflows `bit_buffer`, requiring a split write:
  //    - Fill the current `bit_buffer` to 64 bits.
  //    - Flush `bit_buffer` to `io_buffer`.
  //    - Place the remaining bits of the code into the new (empty)
  //    `bit_buffer`.

  uint64_t bit_buffer = 0;
  int bit_count = 0;
  io_buffer = malloc(HUFF_IO_BUFFER_CAP);
  if (!io_buffer) {
    res = HUFF_ERROR_MEMORY;
    goto cleanup;
  }
  size_t io_pos = 0;

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  for (size_t i = 0; i < size; ++i) {
    uint8_t symbol = data[i];
    FastHuffCode fc = fast_codes[symbol];

    if (fc.len > 0) {
      // Fast path: code fits in 64 bits (true for 99.9% of cases)

      // Check if we can append the code without overflowing the 64-bit buffer
      if (bit_count + fc.len <= 64) {
        // Append bits: shift new bits to the left of existing bits
        bit_buffer |= fc.bits << bit_count;
        bit_count += fc.len;

        // If buffer is exactly full, flush it
        if (bit_count == 64) {
          if (io_pos + 8 > HUFF_IO_BUFFER_CAP) {
            if (fwrite(io_buffer, 1, io_pos, out) != io_pos) {
              res = HUFF_ERROR_FILE_WRITE;
              goto cleanup;
            }
            io_pos = 0;
          }
          // Unroll 8-byte write (Little Endian)
          io_buffer[io_pos++] = (uint8_t)(bit_buffer);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 8);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 16);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 24);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 32);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 40);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 48);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 56);

          bit_buffer = 0;
          bit_count = 0;
        }
      } else {
        // Buffer overflow: Split the code across two 64-bit words

        // Fill the remaining space in the current buffer
        bit_buffer |= fc.bits << bit_count;

        // Flush the full 64-bit buffer
        if (io_pos + 8 > HUFF_IO_BUFFER_CAP) {
          if (fwrite(io_buffer, 1, io_pos, out) != io_pos) {
            res = HUFF_ERROR_FILE_WRITE;
            goto cleanup;
          }
          io_pos = 0;
        }

        // Write 8 bytes manually (Little Endian)
        io_buffer[io_pos++] = (uint8_t)(bit_buffer);
        io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 8);
        io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 16);
        io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 24);
        io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 32);
        io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 40);
        io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 48);
        io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 56);

        // Calculate how many bits were written and put the rest in the new
        // buffer
        int written = 64 - bit_count;
        bit_buffer = fc.bits >> written;  // Shift out the bits we just wrote
        bit_count = fc.len - written;     // Update count with remaining bits
      }
    } else {
      // Slow path: code > 64 bits (extremely rare, only for degenerate trees)
      // Fallback to byte-by-byte writing
      const HuffCode* c = &codes[symbol];
      for (int b = 0; b < c->bit_count; ++b) {
        if ((c->bits[b >> 3] >> (b & 7)) & 1) {
          bit_buffer |= (1ULL << bit_count);
        }
        bit_count++;
        if (bit_count == 64) {
          if (io_pos + 8 > HUFF_IO_BUFFER_CAP) {
            if (fwrite(io_buffer, 1, io_pos, out) != io_pos) {
              res = HUFF_ERROR_FILE_WRITE;
              goto cleanup;
            }
            io_pos = 0;
          }
          io_buffer[io_pos++] = (uint8_t)(bit_buffer);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 8);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 16);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 24);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 32);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 40);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 48);
          io_buffer[io_pos++] = (uint8_t)(bit_buffer >> 56);
          bit_buffer = 0;
          bit_count = 0;
        }
      }
    }
  }

  // Flush remaining bits
  while (bit_count > 0) {
    if (io_pos >= HUFF_IO_BUFFER_CAP) {
      if (fwrite(io_buffer, 1, io_pos, out) != io_pos) {
        res = HUFF_ERROR_FILE_WRITE;
        goto cleanup;
      }
      io_pos = 0;
    }
    io_buffer[io_pos++] = (uint8_t)(bit_buffer & 0xFF);
    bit_buffer >>= 8;
    bit_count -= 8;
  }

  // Flush remaining bytes in buffer
  if (io_pos > 0) {
    if (fwrite(io_buffer, 1, io_pos, out) != io_pos) {
      res = HUFF_ERROR_FILE_WRITE;
      goto cleanup;
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  double time_taken =
      (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

  long out_size = ftell(out);

  if (stats) {
    stats->original_size = size;
    stats->compressed_size = out_size;
    stats->time_taken = time_taken;

    // Calculate entropy and avg code length for stats
    double entropy = 0.0;
    double avg_code_len = 0.0;
    for (int i = 0; i < HUFF_MAX_SYMBOLS; ++i) {
      if (freq[i] > 0) {
        double p = (double)freq[i] / size;
        entropy -= p * log2(p);
        avg_code_len += p * codes[i].bit_count;
      }
    }
    stats->entropy = entropy;
    stats->avg_code_len = avg_code_len;
    memcpy(stats->codes, codes, sizeof(codes));
  }

  res = HUFF_SUCCESS;
cleanup:
  if (out) {
    fclose(out);
  }
  free(io_buffer);
  free(data);
  return res;
}

HuffResult huffman_decode(const char* input_path, const char* output_path,
                          HuffStats* stats) {
  FILE* in = fopen(input_path, "rb");
  if (!in) {
    return HUFF_ERROR_FILE_OPEN;
  }
  uint64_t original_size = 0;
  uint8_t lengths[HUFF_MAX_SYMBOLS] = {0};
  if (!_huff_read_header(in, &original_size, lengths)) {
    fclose(in);
    return HUFF_ERROR_BAD_FORMAT;
  }

  FILE* out = fopen(output_path, "wb");
  if (!out) {
    fclose(in);
    return HUFF_ERROR_FILE_OPEN;
  }
  if (original_size == 0) {
    fclose(out);
    fclose(in);
    return HUFF_SUCCESS;
  }

  // Check for single symbol case optimization
  // If the file contains only one unique symbol, the Huffman tree is trivial.
  // The encoder assigns a 1-bit dummy code (0) to this symbol to ensure
  // a valid bitstream. However, for decoding, we can simply memset the
  // output buffer with the symbol value, which is orders of magnitude faster
  // than processing the bitstream bit-by-bit.
  int unique = 0;
  int last_symbol = 0;
  for (int i = 0; i < HUFF_MAX_SYMBOLS; ++i) {
    if (lengths[i] > 0) {
      unique++;
      last_symbol = i;
    }
  }

  if (unique == 1) {
    uint8_t value = (uint8_t)last_symbol;
    const size_t block_cap = 4096;
    uint8_t block[block_cap];
    memset(block, value, block_cap);
    uint64_t remaining = original_size;
    while (remaining > 0) {
      size_t chunk = remaining > block_cap ? block_cap : (size_t)remaining;
      if (fwrite(block, 1, chunk, out) != chunk) {
        fclose(out);
        fclose(in);
        return HUFF_ERROR_FILE_WRITE;
      }
      remaining -= chunk;
    }
    fclose(out);
    fclose(in);
    return HUFF_SUCCESS;
  }

  HuffCode codes[HUFF_MAX_SYMBOLS];
  _huff_make_canonical(lengths, codes);

  HuffNode nodes[HUFF_MAX_NODES] = {0};
  int node_count = 0;
  int root = _huff_rebuild_tree(codes, nodes, &node_count);
  if (root < 0) {
    fclose(out);
    fclose(in);
    return HUFF_ERROR_BAD_FORMAT;
  }

  // Build lookup table for faster decoding
  HuffDecEntry table[HUFF_DEC_TABLE_SIZE];
  for (int i = 0; i < HUFF_DEC_TABLE_SIZE; ++i) {
    int node = root;
    int bits = 0;
    // Simulate walking the tree with bits of i (LSB first)
    for (int b = 0; b < HUFF_DEC_TABLE_BITS; ++b) {
      int bit = (i >> b) & 1;
      node = bit ? nodes[node].right : nodes[node].left;
      bits++;
      if (node < 0) break;  // Should not happen if tree is valid
      if (nodes[node].left < 0 && nodes[node].right < 0) {
        // Leaf found
        table[i].symbol = (int16_t)nodes[node].symbol;
        table[i].bits = (uint8_t)bits;
        table[i].next_node = -1;
        goto next_entry;
      }
    }
    // Not a leaf after HUFF_DEC_TABLE_BITS
    table[i].symbol = -1;
    table[i].bits = HUFF_DEC_TABLE_BITS;
    table[i].next_node = (int16_t)node;
  next_entry:;
  }

  // Output buffer
  uint8_t* out_buffer = malloc(HUFF_IO_BUFFER_CAP);
  if (!out_buffer) {
    fclose(out);
    fclose(in);
    return HUFF_ERROR_MEMORY;
  }
  size_t out_pos = 0;

  BitReader reader;
  _huff_bit_reader_init(&reader, in);
  uint64_t produced = 0;

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  HuffResult res = HUFF_SUCCESS;

  while (produced < original_size) {
    _huff_bit_reader_ensure(&reader, HUFF_DEC_TABLE_BITS);

    // Peek bits
    uint16_t peek = (uint16_t)(reader.bit_buffer & (HUFF_DEC_TABLE_SIZE - 1));
    HuffDecEntry* entry = &table[peek];

    if (entry->symbol >= 0) {
      // Fast path: symbol found in table
      if (reader.bit_count < entry->bits) {
        res = HUFF_ERROR_BAD_FORMAT;
        goto decode_error;
      }

      out_buffer[out_pos++] = (uint8_t)entry->symbol;
      if (out_pos == HUFF_IO_BUFFER_CAP) {
        if (fwrite(out_buffer, 1, out_pos, out) != out_pos) {
          res = HUFF_ERROR_FILE_WRITE;
          goto decode_error;
        }
        out_pos = 0;
      }

      reader.bit_buffer >>= entry->bits;
      reader.bit_count -= entry->bits;
    } else {
      // Slow path: consume table bits and continue walking
      if (reader.bit_count < HUFF_DEC_TABLE_BITS) {
        res = HUFF_ERROR_BAD_FORMAT;
        goto decode_error;
      }
      reader.bit_buffer >>= HUFF_DEC_TABLE_BITS;
      reader.bit_count -= HUFF_DEC_TABLE_BITS;

      int node_index = entry->next_node;
      while (nodes[node_index].left >= 0 || nodes[node_index].right >= 0) {
        // Inline bit reading
        if (reader.bit_count == 0) {
          if (reader.io_pos >= reader.io_end) {
            if (!_huff_bit_reader_fill_io(&reader)) {
              res = HUFF_ERROR_BAD_FORMAT;
              goto decode_error;
            }
          }
          reader.bit_buffer = reader.io_buffer[reader.io_pos++];
          reader.bit_count = 8;
        }

        uint8_t bit = (uint8_t)(reader.bit_buffer & 1);
        reader.bit_buffer >>= 1;
        reader.bit_count--;

        node_index = bit ? nodes[node_index].right : nodes[node_index].left;
        if (node_index < 0) {
          res = HUFF_ERROR_BAD_FORMAT;
          goto decode_error;
        }
      }

      out_buffer[out_pos++] = (uint8_t)nodes[node_index].symbol;
      if (out_pos == HUFF_IO_BUFFER_CAP) {
        if (fwrite(out_buffer, 1, out_pos, out) != out_pos) {
          res = HUFF_ERROR_FILE_WRITE;
          goto decode_error;
        }
        out_pos = 0;
      }
    }
    produced += 1;
  }

  // Flush remaining output
  if (out_pos > 0) {
    if (fwrite(out_buffer, 1, out_pos, out) != out_pos) {
      res = HUFF_ERROR_FILE_WRITE;
      goto decode_error;
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  double time_taken =
      (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

  if (stats) {
    stats->original_size = produced;
    stats->time_taken = time_taken;
  }

  free(out_buffer);
  _huff_bit_reader_free(&reader);
  fclose(out);
  fclose(in);
  return HUFF_SUCCESS;

decode_error:
  free(out_buffer);
  _huff_bit_reader_free(&reader);
  fclose(out);
  fclose(in);
  return res;
}

#endif  // HUFF_IMPLEMENTATION

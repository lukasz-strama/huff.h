/* huff - fast and simple Huffman Encoder/Decoder Library
 *
 * To use this library, define HUFF_IMPLEMENTATION in ONE source file
 * before including this header:
 *
 *    #define HUFF_IMPLEMENTATION
 *    #include "huff.h"
 *
 * Author: Łukasz Strama
 * License: MIT
 */

#ifndef HUFF_H
#define HUFF_H

#include <stdbool.h>
#include <stdint.h>

#define HUFF_MAX_SYMBOLS 256
#define HUFF_MAX_CODE_BITS 256
#define HUFF_MAX_CODE_BYTES ((HUFF_MAX_CODE_BITS + 7) / 8)

typedef struct {
    uint8_t bits[HUFF_MAX_CODE_BYTES];
    uint16_t bit_count;
} HuffCode;

// Statistics structure
typedef struct {
    uint64_t original_size;
    uint64_t compressed_size;
    double time_taken;
    double entropy;
    double avg_code_len;
    HuffCode codes[HUFF_MAX_SYMBOLS];
} HuffStats;

/**
 * @brief Compress a file using Huffman coding.
 * 
 * @param input_path Path to the input file.
 * @param output_path Path to the output file.
 * @param stats Optional pointer to HuffStats to populate with compression statistics.
 * @return true on success, false on failure.
 */
bool huffman_encode(const char *input_path, const char *output_path, HuffStats *stats);

/**
 * @brief Decompress a Huffman encoded file.
 * 
 * @param input_path Path to the input file.
 * @param output_path Path to the output file.
 * @param stats Optional pointer to HuffStats to populate with decompression statistics.
 * @return true on success, false on failure.
 */
bool huffman_decode(const char *input_path, const char *output_path, HuffStats *stats);

/**
 * @brief Display the Huffman tree from a compressed file to stdout.
 * 
 * @param input_path Path to the compressed file.
 * @return true on success, false on failure.
 */
bool huffman_show_tree(const char *input_path);

/**
 * @brief Print the code table to stdout.
 * 
 * @param codes Array of HuffCode structures.
 */
void huffman_print_code_table(const HuffCode *codes);

#endif // HUFF_H

#ifdef HUFF_IMPLEMENTATION

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

// --- Constants & Macros ---

#define HUFF_MAGIC "HUF1"
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
    int16_t symbol;    // 0-255 if leaf, -1 if not
    uint8_t bits;      // Number of bits to consume
    int16_t next_node; // Next node index if not leaf
} HuffDecEntry;

typedef struct {
    FILE *file;
    uint64_t bit_buffer;
    uint32_t bit_count;
    uint8_t *io_buffer;
    size_t io_pos;
    size_t io_end;
    bool exhausted;
} BitReader;

typedef struct {
	int data[HUFF_MAX_NODES];
	size_t size;
	HuffNode *nodes;
} HuffHeap;

typedef struct {
    const uint8_t *data;
    size_t size;
    uint64_t freq[HUFF_MAX_SYMBOLS];
} FreqThreadArgs;

// --- Internal Function Prototypes ---

static void report_errno(const char *path);
static void report_error(const char *message);
static void format_size(uint64_t bytes, char *buffer, size_t buffer_size);

static void bit_reader_init(BitReader *reader, FILE *file);
static bool bit_reader_fill_io(BitReader *reader);
static void bit_reader_free(BitReader *reader);

static bool heap_less(const HuffHeap *heap, int a, int b);
static void heap_swap(int *a, int *b);
static bool heap_push(HuffHeap *heap, int index);
static int heap_pop(HuffHeap *heap);

static int huff_build_tree(const uint64_t freq[HUFF_MAX_SYMBOLS], HuffNode *nodes, int *out_count);
static void huff_collect_codes_rec(const HuffNode *nodes, int node_index, HuffCode *codes, uint8_t *path, uint16_t depth);
static void huff_collect_codes(const HuffNode *nodes, int root, HuffCode *codes);

static bool read_entire_file(const char *path, uint8_t **data, size_t *size);
static bool huff_write_header(FILE *out, uint64_t original_size, const uint64_t freq[HUFF_MAX_SYMBOLS]);
static bool huff_read_header(FILE *in, uint64_t *original_size, uint64_t freq[HUFF_MAX_SYMBOLS]);

static void *freq_worker(void *arg);
static void parallel_freq_count(const uint8_t *data, size_t size, uint64_t freq[HUFF_MAX_SYMBOLS]);

// --- Error Reporting & Utils ---

static void report_errno(const char *path) {
	fprintf(stderr, "%s: %s\n", path, strerror(errno));
}

static void report_error(const char *message) {
	fprintf(stderr, "%s\n", message);
}

// Format file size to human-readable string (B, KB, MB...)
static void format_size(uint64_t bytes, char *buffer, size_t buffer_size) {
    const char *suffixes[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double size = (double)bytes;
    while (size >= 1024 && i < 4) {
        size /= 1024;
        i++;
    }
    snprintf(buffer, buffer_size, "%.2f %s", size, suffixes[i]);
}

// --- BitReader Implementation ---

static void bit_reader_init(BitReader *reader, FILE *file) {
    reader->file = file;
    reader->bit_buffer = 0;
    reader->bit_count = 0;
    reader->io_buffer = malloc(HUFF_IO_BUFFER_CAP);
    reader->io_pos = 0;
    reader->io_end = 0;
    reader->exhausted = false;
}

static bool bit_reader_fill_io(BitReader *reader) {
    reader->io_pos = 0;
    size_t n = fread(reader->io_buffer, 1, HUFF_IO_BUFFER_CAP, reader->file);
    reader->io_end = n;
    return n > 0;
}

static void bit_reader_ensure(BitReader *reader, uint32_t n) {
    while (reader->bit_count < n) {
        if (reader->io_pos >= reader->io_end) {
            if (!bit_reader_fill_io(reader)) {
                reader->exhausted = true;
                break;
            }
        }
        reader->bit_buffer |= (uint64_t)reader->io_buffer[reader->io_pos++] << reader->bit_count;
        reader->bit_count += 8;
    }
}

static void bit_reader_free(BitReader *reader) {
    free(reader->io_buffer);
    reader->io_buffer = NULL;
}

// --- Heap Implementation ---

static bool heap_less(const HuffHeap *heap, int a, int b) {
	const HuffNode *nodes = heap->nodes;
	if (nodes[a].weight < nodes[b].weight) return true;
	if (nodes[a].weight > nodes[b].weight) return false;
	return a < b;
}

static void heap_swap(int *a, int *b) {
	int tmp = *a;
	*a = *b;
	*b = tmp;
}

static bool heap_push(HuffHeap *heap, int index) {
	if (heap->size >= HUFF_MAX_NODES) {
		return false;
	}
	size_t i = heap->size;
	heap->data[i] = index;
	heap->size += 1;
	while (i > 0) {
		size_t parent = (i - 1) / 2;
		if (!heap_less(heap, heap->data[i], heap->data[parent])) {
			break;
		}
		heap_swap(&heap->data[i], &heap->data[parent]);
		i = parent;
	}
	return true;
}

static int heap_pop(HuffHeap *heap) {
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
			if (left < heap->size && heap_less(heap, heap->data[left], heap->data[smallest])) {
				smallest = left;
			}
			if (right < heap->size && heap_less(heap, heap->data[right], heap->data[smallest])) {
				smallest = right;
			}
			if (smallest == i) {
				break;
			}
			heap_swap(&heap->data[i], &heap->data[smallest]);
			i = smallest;
		}
	}
	return root;
}

// --- Huffman Tree & Codes ---

static int huff_build_tree(const uint64_t freq[HUFF_MAX_SYMBOLS], HuffNode *nodes, int *out_count) {
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
		if (!heap_push(&heap, count)) {
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
		int a = heap_pop(&heap);
		int b = heap_pop(&heap);
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
		if (!heap_push(&heap, count)) {
			return -1;
		}
		count += 1;
	}
	*out_count = count;
	return heap.size == 1 ? heap.data[0] : -1;
}

static void huff_collect_codes_rec(const HuffNode *nodes, int node_index, HuffCode *codes, uint8_t *path, uint16_t depth) {
	const HuffNode *node = &nodes[node_index];
    // If leaf, save the code
	if (node->left < 0 && node->right < 0) {
		HuffCode *code = &codes[node->symbol];
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
		huff_collect_codes_rec(nodes, node->left, codes, path, depth + 1);
	}
	if (node->right >= 0) {
		path[depth] = 1;
		huff_collect_codes_rec(nodes, node->right, codes, path, depth + 1);
	}
}

static void huff_collect_codes(const HuffNode *nodes, int root, HuffCode *codes) {
	memset(codes, 0, sizeof(HuffCode) * HUFF_MAX_SYMBOLS);
	uint8_t path[HUFF_MAX_CODE_BITS] = {0};
	huff_collect_codes_rec(nodes, root, codes, path, 0);
}

void huffman_print_code_table(const HuffCode *codes) {
    printf("--- Huffman Code Table ---\n");
    for (int i = 0; i < HUFF_MAX_SYMBOLS; ++i) {
        if (codes[i].bit_count > 0) {
            printf("Symbol 0x%02X: ", i);
            if (i >= 32 && i <= 126) printf("'%c' ", (char)i);
            else printf("    ");
            
            for (int j = 0; j < codes[i].bit_count; ++j) {
                printf("%d", (codes[i].bits[j >> 3] >> (j & 7)) & 1);
            }
            printf(" (%d bits)\n", codes[i].bit_count);
        }
    }
    printf("--------------------------\n");
}

// --- File I/O Helpers ---

static bool read_entire_file(const char *path, uint8_t **data, size_t *size) {
	FILE *file = fopen(path, "rb");
	if (!file) {
		report_errno(path);
		return false;
	}
    // Check file size
	if (fseeko(file, 0, SEEK_END) != 0) {
		report_errno(path);
		fclose(file);
		return false;
	}
	off_t length = ftello(file);
	if (length < 0) {
		report_errno(path);
		fclose(file);
		return false;
	}
	if (fseeko(file, 0, SEEK_SET) != 0) {
		report_errno(path);
		fclose(file);
		return false;
	}
	if ((uint64_t)length > SIZE_MAX) {
		report_error("input too large");
		fclose(file);
		return false;
	}
	size_t buffer_size = (size_t)length;
	uint8_t *buffer = NULL;
	if (buffer_size > 0) {
		buffer = malloc(buffer_size);
		if (!buffer) {
			report_error("out of memory");
			fclose(file);
			return false;
		}
        // Read entire file into RAM
		size_t read_bytes = fread(buffer, 1, buffer_size, file);
		if (read_bytes != buffer_size) {
			report_errno(path);
			free(buffer);
			fclose(file);
			return false;
		}
	}
	fclose(file);
	*data = buffer;
	*size = buffer_size;
	return true;
}

static bool huff_write_header(FILE *out, uint64_t original_size, const uint64_t freq[HUFF_MAX_SYMBOLS]) {
	if (fwrite(HUFF_MAGIC, 1, 4, out) != 4) return false;
	if (fwrite(&original_size, sizeof(original_size), 1, out) != 1) return false;
	if (fwrite(freq, sizeof(uint64_t), HUFF_MAX_SYMBOLS, out) != HUFF_MAX_SYMBOLS) return false;
	return true;
}

static bool huff_read_header(FILE *in, uint64_t *original_size, uint64_t freq[HUFF_MAX_SYMBOLS]) {
	char magic[4];
	if (fread(magic, 1, 4, in) != 4) return false;
	if (memcmp(magic, HUFF_MAGIC, 4) != 0) {
		report_error("bad magic");
		return false;
	}
	if (fread(original_size, sizeof(*original_size), 1, in) != 1) return false;
	if (fread(freq, sizeof(uint64_t), HUFF_MAX_SYMBOLS, in) != HUFF_MAX_SYMBOLS) return false;
	return true;
}

// --- Threading Helpers ---

// Worker function for frequency counting thread
// Uses loop unrolling
static void *freq_worker(void *arg) {
    FreqThreadArgs *args = (FreqThreadArgs *)arg;
    memset(args->freq, 0, sizeof(args->freq));
    size_t i = 0;
    // Process 8 bytes at a time
    for (; i + 8 <= args->size; i += 8) {
        args->freq[args->data[i+0]]++;
        args->freq[args->data[i+1]]++;
        args->freq[args->data[i+2]]++;
        args->freq[args->data[i+3]]++;
        args->freq[args->data[i+4]]++;
        args->freq[args->data[i+5]]++;
        args->freq[args->data[i+6]]++;
        args->freq[args->data[i+7]]++;
    }
    // Finish remaining bytes
    for (; i < args->size; ++i) {
        args->freq[args->data[i]]++;
    }
    return NULL;
}

// Parallel symbol frequency counting
// Splits data into chunks and launches threads (max 64)
static void parallel_freq_count(const uint8_t *data, size_t size, uint64_t freq[HUFF_MAX_SYMBOLS]) {
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores < 1) num_cores = 1;
    if (size < 1024 * 1024) num_cores = 1; // Small files processed single-threaded

    pthread_t threads[64];
    FreqThreadArgs args[64];
    if (num_cores > 64) num_cores = 64;

    size_t chunk_size = size / num_cores;
    for (int i = 0; i < num_cores; ++i) {
        args[i].data = data + i * chunk_size;
        args[i].size = (i == num_cores - 1) ? (size - i * chunk_size) : chunk_size;
        pthread_create(&threads[i], NULL, freq_worker, &args[i]);
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

bool huffman_encode(const char *input_path, const char *output_path, HuffStats *stats) {
	uint8_t *data = NULL;
	size_t size = 0;
	FILE *out = NULL;
	bool ok = false;
	if (!read_entire_file(input_path, &data, &size)) {
		goto cleanup;
	}
	out = fopen(output_path, "wb");
	if (!out) {
		report_errno(output_path);
		goto cleanup;
	}
	uint64_t freq[HUFF_MAX_SYMBOLS] = {0};
    parallel_freq_count(data, size, freq);
	if (!huff_write_header(out, (uint64_t)size, freq)) {
		report_errno(output_path);
		goto cleanup;
	}
	if (size == 0) {
		ok = true;
		goto cleanup;
	}
	HuffNode nodes[HUFF_MAX_NODES] = {0};
	int node_count = 0;
	int root = huff_build_tree(freq, nodes, &node_count);
	if (root < 0) {
		report_error("failed to build tree");
		goto cleanup;
	}
    HuffCode codes[HUFF_MAX_SYMBOLS];
    huff_collect_codes(nodes, root, codes);
    
    // Precompute fast codes (up to 64 bits)
    typedef struct {
        uint64_t bits;
        int len;
    } FastHuffCode;
    
    FastHuffCode fast_codes[HUFF_MAX_SYMBOLS];
    for (int i = 0; i < HUFF_MAX_SYMBOLS; ++i) {
        if (codes[i].bit_count > 64) {
            fast_codes[i].len = -1; // Too long for fast path
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

    // Optimized bit writer state
    uint64_t bit_buffer = 0;
    int bit_count = 0;
    uint8_t *io_buffer = malloc(HUFF_IO_BUFFER_CAP);
    if (!io_buffer) {
        report_error("out of memory");
        goto cleanup;
    }
    size_t io_pos = 0;
    
    clock_t start_time = clock();
    
    for (size_t i = 0; i < size; ++i) {
        uint8_t symbol = data[i];
        FastHuffCode fc = fast_codes[symbol];
        
        if (fc.len > 0) {
            // Fast path: code fits in 64 bits
            if (bit_count + fc.len <= 64) {
                bit_buffer |= fc.bits << bit_count;
                bit_count += fc.len;
            } else {
                // Buffer full, split write
                bit_buffer |= fc.bits << bit_count;
                
                // Flush 64 bits (8 bytes)
                if (io_pos + 8 > HUFF_IO_BUFFER_CAP) {
                    if (fwrite(io_buffer, 1, io_pos, out) != io_pos) {
                        report_errno(output_path);
                        free(io_buffer);
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
                
                int written = 64 - bit_count;
                bit_buffer = fc.bits >> written;
                bit_count = fc.len - written;
            }
        } else {
            // Slow path: code > 64 bits (rare)
            // Fallback to byte-by-byte writing
            const HuffCode *c = &codes[symbol];
            for (int b = 0; b < c->bit_count; ++b) {
                if ((c->bits[b >> 3] >> (b & 7)) & 1) {
                    bit_buffer |= (1ULL << bit_count);
                }
                bit_count++;
                if (bit_count == 64) {
                    if (io_pos + 8 > HUFF_IO_BUFFER_CAP) {
                        if (fwrite(io_buffer, 1, io_pos, out) != io_pos) {
                            report_errno(output_path);
                            free(io_buffer);
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
                report_errno(output_path);
                free(io_buffer);
                goto cleanup;
            }
            io_pos = 0;
        }
        io_buffer[io_pos++] = (uint8_t)(bit_buffer & 0xFF);
        bit_buffer >>= 8;
        bit_count -= 8; // May become negative, handled by loop condition? No.
        // bit_count is number of valid bits.
        // If we have 3 bits, we write 1 byte.
        // The loop above writes full bytes.
        // We need to be careful.
    }
    // Correct flush logic:
    // We have `bit_count` bits in `bit_buffer`.
    // We need to write `ceil(bit_count / 8)` bytes.
    // But `bit_buffer` is shifted out.
    // Let's rewrite the flush loop.
    
    // Flush remaining bytes in buffer
    if (io_pos > 0) {
        if (fwrite(io_buffer, 1, io_pos, out) != io_pos) {
            report_errno(output_path);
            free(io_buffer);
            goto cleanup;
        }
    }
    free(io_buffer);
    
    clock_t end_time = clock();
    double time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
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

    ok = true;cleanup:
	if (out) {
		fclose(out);
	}
	free(data);
	return ok;
}

bool huffman_decode(const char *input_path, const char *output_path, HuffStats *stats) {
	FILE *in = fopen(input_path, "rb");
	if (!in) {
		report_errno(input_path);
		return false;
	}
	uint64_t original_size = 0;
	uint64_t freq[HUFF_MAX_SYMBOLS] = {0};
	if (!huff_read_header(in, &original_size, freq)) {
		report_errno(input_path);
		fclose(in);
		return false;
	}
	uint64_t sum = 0;
	int unique = 0;
	int last_symbol = 0;
	for (int i = 0; i < HUFF_MAX_SYMBOLS; ++i) {
		if (freq[i] > 0) {
			sum += freq[i];
			unique += 1;
			last_symbol = i;
		}
	}
	if (sum != original_size) {
		report_error("frequency sum mismatch");
		fclose(in);
		return false;
	}
	FILE *out = fopen(output_path, "wb");
	if (!out) {
		report_errno(output_path);
		fclose(in);
		return false;
	}
	if (original_size == 0) {
		fclose(out);
		fclose(in);
		return true;
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
				report_errno(output_path);
				fclose(out);
				fclose(in);
				return false;
			}
			remaining -= chunk;
		}
		fclose(out);
		fclose(in);
		return true;
	}
	HuffNode nodes[HUFF_MAX_NODES] = {0};
	int node_count = 0;
	int root = huff_build_tree(freq, nodes, &node_count);
    if (root < 0) {
		report_error("failed to rebuild tree");
		fclose(out);
		fclose(in);
		return false;
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
            if (node < 0) break; // Should not happen if tree is valid
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
    uint8_t *out_buffer = malloc(HUFF_IO_BUFFER_CAP);
    if (!out_buffer) {
        report_error("out of memory");
        fclose(out);
        fclose(in);
        return false;
    }
    size_t out_pos = 0;

    BitReader reader;
    bit_reader_init(&reader, in);
    uint64_t produced = 0;
    
    clock_t start_time = clock();
    
    while (produced < original_size) {
        bit_reader_ensure(&reader, HUFF_DEC_TABLE_BITS);
        
        // Peek bits
        uint16_t peek = (uint16_t)(reader.bit_buffer & (HUFF_DEC_TABLE_SIZE - 1));
        HuffDecEntry *entry = &table[peek];
        
        if (entry->symbol >= 0) {
            // Fast path: symbol found in table
            if (reader.bit_count < entry->bits) {
                if (reader.bit_count < entry->bits) {
                     report_error("unexpected end of stream");
                     goto decode_error;
                }
            }
            
            out_buffer[out_pos++] = (uint8_t)entry->symbol;
            if (out_pos == HUFF_IO_BUFFER_CAP) {
                if (fwrite(out_buffer, 1, out_pos, out) != out_pos) {
                    report_errno(output_path);
                    goto decode_error;
                }
                out_pos = 0;
            }

            reader.bit_buffer >>= entry->bits;
            reader.bit_count -= entry->bits;
        } else {
            // Slow path: consume table bits and continue walking
            if (reader.bit_count < HUFF_DEC_TABLE_BITS) {
                 report_error("unexpected end of stream");
                 goto decode_error;
            }
            reader.bit_buffer >>= HUFF_DEC_TABLE_BITS;
            reader.bit_count -= HUFF_DEC_TABLE_BITS;
            
            int node_index = entry->next_node;
            while (nodes[node_index].left >= 0 || nodes[node_index].right >= 0) {
                // Inline bit reading
                if (reader.bit_count == 0) {
                    if (reader.io_pos >= reader.io_end) {
                        if (!bit_reader_fill_io(&reader)) {
                            report_error("unexpected end of stream");
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
                    report_error("corrupted bitstream");
                    goto decode_error;
                }
            }
            
            out_buffer[out_pos++] = (uint8_t)nodes[node_index].symbol;
            if (out_pos == HUFF_IO_BUFFER_CAP) {
                if (fwrite(out_buffer, 1, out_pos, out) != out_pos) {
                    report_errno(output_path);
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
            report_errno(output_path);
            goto decode_error;
        }
    }
    
    clock_t end_time = clock();
    double time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    if (stats) {
        stats->original_size = produced;
        stats->time_taken = time_taken;
    }

    free(out_buffer);
    bit_reader_free(&reader);
    fclose(out);
    fclose(in);
    return true;

decode_error:
    free(out_buffer);
    bit_reader_free(&reader);
    fclose(out);
    fclose(in);
    return false;
}

bool huffman_show_tree(const char *input_path) {
	FILE *in = fopen(input_path, "rb");
	if (!in) {
		report_errno(input_path);
		return false;
	}
	uint64_t original_size = 0;
	uint64_t freq[HUFF_MAX_SYMBOLS] = {0};
	if (!huff_read_header(in, &original_size, freq)) {
		report_errno(input_path);
		fclose(in);
		return false;
	}
    fclose(in);

	HuffNode nodes[HUFF_MAX_NODES] = {0};
	int node_count = 0;
	int root = huff_build_tree(freq, nodes, &node_count);
	if (root < 0) {
		report_error("failed to rebuild tree");
		return false;
	}
    HuffCode codes[HUFF_MAX_SYMBOLS];
    huff_collect_codes(nodes, root, codes);
    
    printf("File: %s\n", input_path);
    char size_buf[32];
    format_size(original_size, size_buf, sizeof(size_buf));
    printf("Original Size: %lu bytes (%s)\n", original_size, size_buf);
    printf("\n");
    huffman_print_code_table(codes);

    return true;
}

#endif // HUFF_IMPLEMENTATION
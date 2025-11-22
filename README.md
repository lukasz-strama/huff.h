# huff.h

A single-header C99 library implementing static Huffman coding with focus on performance.

This library provides a portable*, thread-safe implementation of the Huffman compression algorithm. It is designed primarily for educational purposes and as a reference implementation for academic study.

## Characteristics

*   **Language**: C99 standard.
*   **Type**: Single-header library (STB style).
*   **Algorithm**: Static Huffman coding (two-pass).
*   **Dependencies**: Standard C library (`stdlib.h`, `stdio.h`, etc.) and POSIX threads (`pthread.h`).
*   **Thread Safety**: Reentrant API with no global state.

## Implementation Details

*   **Canonical Huffman Codes**: Uses code lengths to reconstruct trees, minimizing header overhead (256 bytes for lengths).
*   **Table-Based Decoding**: Accelerates decoding using a 12-bit Lookup Table (LUT) to process multiple bits per cycle.
*   **Parallelization**: Utilizes `pthread` to parallelize the frequency counting phase during encoding.
*   **Bit-Level Optimization**: Implements a 64-bit buffered bit writer to reduce I/O overhead and function call costs.

## Limitations & Weak Points

1.  **Memory Consumption (Encoder)**: The encoder reads the **entire input file into memory** to perform parallel frequency counting and fast encoding. This limits the maximum file size to available RAM and makes it unsuitable for very large files or memory-constrained environments.
2.  **Two-Pass Nature**: Being a static Huffman implementation, it requires two passes over the data (one for frequency counting, one for encoding). It cannot be used for streaming data where the full content is not known in advance.
3.  **Portability**: The library relies on POSIX threads (`pthread`) and `sysconf` for parallelization. It is not natively compatible with Windows (MSVC) without a compatibility layer (e.g., pthreads-win32).
4.  **Compression Ratio**: As a pure entropy coder, it does not perform dictionary-based compression (like LZ77). Its compression ratio will be significantly lower than general-purpose tools like `gzip`, `zstd`, or `xz`.

## Benchmarks

Tests were performed on the [Silesia Compression Corpus](https://sun.aei.polsl.pl/~sdeor/index.php?page=silesia).

| File | Original Size | Compressed Size | Ratio | Comp Speed | Decomp Speed |
|------|---------------|-----------------|-------|------------|--------------|
| dickens | 10.19 MB | 5.83 MB | 1.75x | 452.38 MB/s | 218.58 MB/s |
| mozilla | 51.22 MB | 39.98 MB | 1.28x | 392.73 MB/s | 220.87 MB/s |
| mr | 9.97 MB | 4.62 MB | 2.16x | 488.52 MB/s | 258.52 MB/s |
| nci | 33.55 MB | 10.22 MB | 3.28x | 590.63 MB/s | 255.93 MB/s |
| ooffice | 6.15 MB | 5.12 MB | 1.20x | 334.36 MB/s | 201.92 MB/s |
| osdb | 10.09 MB | 8.34 MB | 1.21x | 383.25 MB/s | 211.75 MB/s |
| reymont | 6.63 MB | 4.03 MB | 1.64x | 376.28 MB/s | 202.27 MB/s |
| samba | 21.61 MB | 16.55 MB | 1.31x | 401.33 MB/s | 211.00 MB/s |
| sao | 7.25 MB | 6.84 MB | 1.06x | 370.94 MB/s | 206.30 MB/s |
| webster | 41.46 MB | 25.93 MB | 1.60x | 387.96 MB/s | 204.40 MB/s |
| x-ray | 8.47 MB | 7.02 MB | 1.21x | 362.74 MB/s | 246.82 MB/s |
| xml | 5.35 MB | 3.71 MB | 1.44x | 369.90 MB/s | 222.18 MB/s |

*System: Linux (Fedora), Single-threaded decoding, Multi-threaded frequency counting.*

## Reference Benchmarks (ANSI C Implementation)

Results from [Michael Dipperstein's ANSI C implementation](https://github.com/MichaelDipperstein/huffman) for comparison.

| File | Original Size | Compressed Size | Ratio | Comp Speed | Decomp Speed |
|------|---------------|-----------------|-------|------------|--------------|
| dickens | 10.19 MB | 5.83 MB | 1.75x | 41.61 MB/s | 45.05 MB/s |
| mozilla | 51.22 MB | 39.98 MB | 1.28x | 43.95 MB/s | 40.87 MB/s |
| mr | 9.97 MB | 4.62 MB | 2.16x | 57.01 MB/s | 66.09 MB/s |
| nci | 33.55 MB | 10.22 MB | 3.28x | 58.60 MB/s | 80.60 MB/s |
| ooffice | 6.15 MB | 5.13 MB | 1.20x | 39.59 MB/s | 36.01 MB/s |
| osdb | 10.09 MB | 8.34 MB | 1.21x | 38.33 MB/s | 39.22 MB/s |
| reymont | 6.63 MB | 4.03 MB | 1.64x | 41.02 MB/s | 44.07 MB/s |
| samba | 21.61 MB | 16.55 MB | 1.31x | 40.12 MB/s | 40.23 MB/s |
| sao | 7.25 MB | 6.85 MB | 1.06x | 41.27 MB/s | 34.74 MB/s |
| webster | 41.46 MB | 25.93 MB | 1.60x | 41.75 MB/s | 44.30 MB/s |
| x-ray | 8.47 MB | 7.02 MB | 1.21x | 51.96 MB/s | 46.53 MB/s |
| xml | 5.35 MB | 3.71 MB | 1.44x | 41.45 MB/s | 43.55 MB/s |


## API Reference

### `huffman_encode`
```c
HuffResult huffman_encode(const char *input_path, const char *output_path, HuffStats *stats);
```
Reads the input file, calculates symbol frequencies, builds a canonical Huffman tree, and writes the compressed output.
*   **Returns**: `HUFF_SUCCESS` (0) on success, or a non-zero error code.

### `huffman_decode`
```c
HuffResult huffman_decode(const char *input_path, const char *output_path, HuffStats *stats);
```
Reads a compressed file, reconstructs the Huffman tree from the header, and decodes the data.

### `HuffStats`
A structure containing performance metrics:
*   `original_size` / `compressed_size`: File sizes in bytes.
*   `time_taken`: Execution time in seconds.
*   `entropy`: Shannon entropy of the source data.
*   `avg_code_len`: Average length of the generated codes.

## Building

This project uses [nob.h](https://github.com/tsoding/nob.h), a minimal build system by [Tsoding](https://github.com/tsoding) (Alexey Kutepov).

To build the project:

```bash
cc -o nob nob.c
./nob
```

This will compile the example application to `build/huff`.

## Usage

1.  Copy `huff.h` to your project.
2.  Define `HUFF_IMPLEMENTATION` in **one** source file before including the header.

```c
#define HUFF_IMPLEMENTATION
#include "huff.h"

int main(void) {
    HuffStats stats = {0};
    
    // Compress
    HuffResult res = huffman_encode("data.bin", "data.huff", &stats);
    if (res == HUFF_SUCCESS) {
        printf("Compressed size: %lu bytes\n", stats.compressed_size);
        printf("Entropy: %.4f bits/symbol\n", stats.entropy);
    } else {
        fprintf(stderr, "Compression failed: error code %d\n", res);
    }

    // Decompress
    huffman_decode("data.huff", "data_out.bin", NULL);
    
    return 0;
}
```

## License

MIT License. See `huff.h` for full text.
# huff

A fast and simple Huffman Encoder/Decoder single-header library for C/C++.

**This library is designed to be one of the fastest pure Huffman implementations available, pushing the algorithm to its theoretical performance limits on modern hardware.**

## Features

*   **Monolithic single-header**: Easy to integrate into any C/C++ project.
*   **High Performance**: Optimized with 12-bit Lookup Tables (LUT) for fast decoding and 64-bit buffer fast-paths for encoding.
*   **Multi-threaded Frequency Counting**: Uses `pthread` to parallelize the initial file scan, significantly speeding up the compression of large files.
*   **Optimized Single-Threaded Core**: The encoding and decoding loops are highly optimized single-threaded implementations that saturate memory bandwidth.
*   **Canonical Huffman**: Uses Canonical Huffman codes to minimize header size (256 bytes for code lengths) and ensure optimal compression for small files.
*   **Efficient I/O**: Uses internal buffering (4KB) to minimize system calls during file operations.
*   **Statistics**: Calculates Shannon entropy, average code length, and coding efficiency.
*   **Portable (POSIX)**: Written in C99. Depends only on the standard library and pthreads (not available on Windows by default).

## Limitations

*   **Memory Usage (Encoder)**: The encoder currently reads the *entire input file into memory* to perform parallel frequency counting and fast encoding. This limits the maximum file size to available RAM. The decoder, however, uses buffered I/O and has a low memory footprint.
*   **Static Huffman**: This is a two-pass static Huffman implementation. It requires the entire dataset to be available to calculate frequencies before encoding begins, making it unsuitable for real-time streams.

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

## Usage

1.  Copy `huff.h` to your project.
2.  Define `HUFF_IMPLEMENTATION` in **one** source file before including the header to create the implementation.

```c
#define HUFF_IMPLEMENTATION
#include "huff.h"

int main(void) {
    HuffStats stats = {0};
    
    // Compress
    if (huffman_encode("data.bin", "data.huff", &stats)) {
        printf("Compressed size: %lu bytes\n", stats.compressed_size);
    }

    // Decompress
    huffman_decode("data.huff", "data_out.bin", NULL);
    
    return 0;
}
```

## API Reference
Compresses the input file using Huffman coding.
```c
bool huffman_encode(const char *input_path, const char *output_path, HuffStats *stats)
```
*   `stats`: Optional pointer to `HuffStats` to retrieve compression metrics (entropy, time, etc.).

Decompresses a Huffman-encoded file.
```c
bool huffman_decode(const char *input_path, const char *output_path, HuffStats *stats)
```

Reconstructs and displays the Huffman tree and code table from a compressed file.
```c
bool huffman_show_tree(const char *input_path)
```

Helper to print the generated Huffman codes to stdout.
```c
void huffman_print_code_table(const HuffCode *codes)
```

## Building

This project uses [nob.h](https://github.com/tsoding/nob.h), a minimal build system by [Tsoding](https://github.com/tsoding) (Alexey Kutepov).

To build the project:

```bash
cc -o nob nob.c
./nob
```

This will compile the example application to `build/huff`.

## Who is this for?

This library is designed as a **high-performance foundation** for other projects. It is ideal for:
*   **Custom Compression Experiments**: Like building your own compression algorithms (e.g., BWT + Huffman, LZ77 + Huffman) who need a fast, reliable entropy coding backend without writing it from scratch.
*   **Quick Integration**: Projects that need standard Huffman coding immediately with zero dependencies and minimal integration effort.
*   **Educational Value**: A clean, modern reference for how to implement Huffman coding in C, demonstrating advanced optimizations like Canonical codes and LUT decoding.

## License

MIT License
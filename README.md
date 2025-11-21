# huff

A fast and simple Huffman Encoder/Decoder single-header library for C/C++.

## Features

*   **Monolithic single-header**: Easy to integrate into any C/C++ project.
*   **High Performance**: Optimized with 12-bit Lookup Tables (LUT) for fast decoding and 64-bit buffer fast-paths for encoding.
*   **Canonical Huffman**: Uses Canonical Huffman codes to minimize header size (256 bytes for code lengths) and ensure optimal compression for small files.
*   **Efficient I/O**: Uses internal buffering (4KB) to minimize system calls during file operations.
*   **Simple API**: High-level functions for file compression and decompression.
*   **Multi-threaded**: Uses `pthread` for parallel frequency counting on large files.
*   **Statistics**: Calculates Shannon entropy, average code length, and coding efficiency.
*   **Portable (POSIX)**: Written in C99. Depends only on the standard library and pthreads (not available on Windows by default).

## Benchmarks

Tests were performed on the [Silesia Compression Corpus](https://sun.aei.polsl.pl/~sdeor/index.php?page=silesia).

| File | Original Size | Compressed Size | Ratio | Comp Speed | Decomp Speed |
|------|---------------|-----------------|-------|------------|--------------|
| dickens | 10.19 MB | 5.83 MB | 1.75x | 441.17 MB/s | 210.09 MB/s |
| mozilla | 51.22 MB | 39.98 MB | 1.28x | 389.59 MB/s | 215.77 MB/s |
| mr | 9.97 MB | 4.62 MB | 2.16x | 490.54 MB/s | 255.48 MB/s |
| nci | 33.55 MB | 10.22 MB | 3.28x | 573.33 MB/s | 246.26 MB/s |
| ooffice | 6.15 MB | 5.12 MB | 1.20x | 334.27 MB/s | 201.86 MB/s |
| osdb | 10.09 MB | 8.34 MB | 1.21x | 372.22 MB/s | 214.99 MB/s |
| reymont | 6.63 MB | 4.03 MB | 1.64x | 398.40 MB/s | 203.69 MB/s |
| samba | 21.61 MB | 16.55 MB | 1.31x | 387.93 MB/s | 209.72 MB/s |
| sao | 7.25 MB | 6.84 MB | 1.06x | 345.44 MB/s | 214.75 MB/s |
| webster | 41.46 MB | 25.93 MB | 1.60x | 406.30 MB/s | 196.73 MB/s |
| x-ray | 8.47 MB | 7.02 MB | 1.21x | 374.71 MB/s | 233.14 MB/s |
| xml | 5.35 MB | 3.71 MB | 1.44x | 375.63 MB/s | 213.25 MB/s |

*System: Linux (Fedora), Single-threaded decoding, Multi-threaded frequency counting.*

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

## License

MIT License

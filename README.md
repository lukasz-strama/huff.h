# huff

A fast and simple Huffman Encoder/Decoder single-header library for C/C++.

## Features

*   **Single-Header**: Easy integration. Just drop `huff.h` into your project.
*   **Simple API**: High-level functions for file compression and decompression.
*   **Multi-threaded**: Uses `pthread` for parallel frequency counting on large files.
*   **Statistics**: Calculates Shannon entropy, average code length, and coding efficiency.
*   **Portable**: Written in C99. Depends only on standard library and pthreads.

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

### `bool huffman_encode(const char *input_path, const char *output_path, HuffStats *stats)`
Compresses the input file using Huffman coding.
*   `stats`: Optional pointer to `HuffStats` to retrieve compression metrics (entropy, time, etc.).

### `bool huffman_decode(const char *input_path, const char *output_path, HuffStats *stats)`
Decompresses a Huffman-encoded file.

### `bool huffman_show_tree(const char *input_path)`
Reconstructs and displays the Huffman tree and code table from a compressed file.

### `void huffman_print_code_table(const HuffCode *codes)`
Helper to print the generated Huffman codes to stdout.

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

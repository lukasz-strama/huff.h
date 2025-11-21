import os
import random

def create_lorem(filename):
    text = """Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum."""
    with open(filename, 'w') as f:
        f.write(text * 1000) # Make it a bit larger

def create_random(filename, size):
    with open(filename, 'wb') as f:
        f.write(os.urandom(size))

def create_pattern(filename, size):
    with open(filename, 'wb') as f:
        # Alternating 0x00 and 0xFF
        data = bytearray([0x00, 0xFF] * (size // 2))
        f.write(data)

def create_skewed(filename, size):
    # Skewed distribution: 'a' appears 50%, 'b' 25%, 'c' 12.5%, etc.
    chars = []
    weights = []
    w = 1000
    for c in "abcdefgh":
        chars.append(c)
        weights.append(w)
        w //= 2
    
    chunk_size = 1024 * 1024 # 1MB chunks
    with open(filename, 'w') as f:
        remaining = size
        while remaining > 0:
            current_chunk = min(remaining, chunk_size)
            # Generate a chunk of characters
            data = random.choices(chars, weights=weights, k=current_chunk)
            f.write("".join(data))
            remaining -= current_chunk

def create_fibonacci(filename):
    # Frequencies following Fibonacci sequence
    # This creates a specific tree structure
    counts = {'a': 1, 'b': 1, 'c': 2, 'd': 3, 'e': 5, 'f': 8, 'g': 13, 'h': 21}
    with open(filename, 'w') as f:
        for char, count in counts.items():
            f.write(char * count * 100)

if __name__ == "__main__":
    print("Generating test files in tests/...")
    os.makedirs("tests", exist_ok=True)
    create_lorem("tests/text_lorem.txt")
    create_random("tests/binary_random_1mb.bin", 1024 * 1024) # 1MB
    create_pattern("tests/binary_pattern.bin", 1024 * 1024)   # 1MB
    create_skewed("tests/text_skewed.txt", 100000000)         # <100MB
    create_fibonacci("tests/text_fibonacci.txt")
    
    # Empty file
    open("tests/empty.txt", 'w').close()
    
    # Single char
    with open("tests/single_char.txt", 'w') as f:
        f.write("A" * 1000)
        
    print("Done.")

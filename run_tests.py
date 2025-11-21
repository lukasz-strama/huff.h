import os
import subprocess
import sys
import glob

def run_test(input_file):
    basename = os.path.basename(input_file)
    compressed_file = os.path.join("tests/outputs", basename + ".huff")
    decompressed_file = os.path.join("tests/outputs", basename + ".decoded")
    
    print(f"Testing {input_file}...")
    
    # Run compression and decompression
    cmd = ["./build/huff", input_file, compressed_file, decompressed_file]
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"  [FAIL] Execution failed for {input_file}")
        print(result.stderr)
        return False

    # Extract stats
    stats = {}
    times = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if line.startswith("Original Size:"):
            stats['orig_size'] = int(line.split(":")[1].strip().split()[0])
        elif line.startswith("Compressed Size:"):
            stats['comp_size'] = int(line.split(":")[1].strip().split()[0])
        elif line.startswith("Entropy:"):
            stats['entropy'] = float(line.split(":")[1].strip().split()[0])
        elif line.startswith("Time Taken:"):
            # First occurrence is compression, second is decompression
            val = line.split(":")[1].strip().split()[0]
            times.append(float(val))
    
    # Verify output matches input
    try:
        with open(input_file, 'rb') as f1, open(decompressed_file, 'rb') as f2:
            if f1.read() != f2.read():
                print(f"  [FAIL] Content mismatch for {input_file}")
                return False
    except Exception as e:
        print(f"  [FAIL] Error verifying files: {e}")
        return False
        
    # Clean up
    # if os.path.exists(compressed_file):
    #     os.remove(compressed_file)
    # if os.path.exists(decompressed_file):
    #     os.remove(decompressed_file)
        
    print(f"  [PASS] {input_file}")
    if 'orig_size' in stats:
        print(f"    Original Size:   {stats['orig_size']:,} bytes")
    if 'comp_size' in stats:
        print(f"    Compressed Size: {stats['comp_size']:,} bytes")
    if 'entropy' in stats:
        print(f"    Entropy:         {stats['entropy']:.4f} bits/symbol")
    
    if stats.get('comp_size', 0) > 0 and stats.get('orig_size', 0) > 0:
        ratio = stats['orig_size'] / stats['comp_size']
        saving = (1.0 - stats['comp_size'] / stats['orig_size']) * 100.0
        print(f"    Compression Rate: {ratio:.2f}x ({saving:.2f}%)")
    
    if len(times) >= 1 and times[0] > 0 and stats.get('orig_size', 0) > 0:
        mb = stats['orig_size'] / (1024 * 1024)
        speed = mb / times[0]
        print(f"    Comp Speed:      {speed:.2f} MB/s ({times[0]:.6f} s)")
        
    if len(times) >= 2 and times[1] > 0 and stats.get('orig_size', 0) > 0:
        mb = stats['orig_size'] / (1024 * 1024)
        speed = mb / times[1]
        print(f"    Decomp Speed:    {speed:.2f} MB/s ({times[1]:.6f} s)")
    
    return True

def main():
    os.makedirs("tests/outputs", exist_ok=True)
    
    test_files = glob.glob("tests/*")
    # Filter out python scripts, directories, and existing .huff/.decoded files
    test_files = [f for f in test_files if os.path.isfile(f) and not f.endswith(".py") and not f.endswith(".huff") and not f.endswith(".decoded")]
    
    passed = 0
    total = 0
    
    for f in test_files:
        total += 1
        if run_test(f):
            passed += 1
                
    print(f"\nSummary: {passed}/{total} tests passed.")
    if passed != total:
        sys.exit(1)

if __name__ == "__main__":
    main()

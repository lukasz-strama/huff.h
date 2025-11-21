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

    # Extract times
    times = []
    for line in result.stdout.splitlines():
        if "Time Taken:" in line:
            times.append(line.split(":")[1].strip())
    
    time_str = ""
    if len(times) >= 2:
        time_str = f" (Comp: {times[0]}, Decomp: {times[1]})"
    elif len(times) == 1:
        time_str = f" (Time: {times[0]})"
        
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
        
    print(f"  [PASS] {input_file}{time_str}")
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

#!/usr/bin/env python3
import sys
import os
import re

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <c_header_file> <output_mp3>", file=sys.stderr)
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else "output.mp3"

    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found.", file=sys.stderr)
        sys.exit(1)

    try:
        with open(input_file, "r") as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading file: {e}", file=sys.stderr)
        sys.exit(1)

    # Find all hex values in the format 0xXX
    hex_values = re.findall(r"0x([0-9a-fA-F]{2})", content)
    
    if not hex_values:
        print("Error: No hex bytes found in the input file.", file=sys.stderr)
        sys.exit(1)

    # Convert hex strings back to binary
    binary_data = bytes(int(h, 16) for h in hex_values)

    try:
        with open(output_file, "wb") as f:
            f.write(binary_data)
        print(f"Successfully reconstructed {output_file} ({len(binary_data)} bytes).")
    except Exception as e:
        print(f"Error writing file: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()

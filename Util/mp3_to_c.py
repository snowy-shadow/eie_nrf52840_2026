#!/usr/bin/env python3
import sys
import os

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <variable_name> <mp3_file>", file=sys.stderr)
        sys.exit(1)

    var_name = sys.argv[1]
    input_file = sys.argv[2]

    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found.", file=sys.stderr)
        sys.exit(1)

    try:
        # Cross-platform: Binary read
        with open(input_file, "rb") as f:
            data = f.read()
    except Exception as e:
        print(f"Error reading file: {e}", file=sys.stderr)
        sys.exit(1)

    # FAST: Pre-calculate hex strings via lookup table
    hex_table = [f"0x{i:02x}" for i in range(256)]
    
    # Process bytes through lookup table
    formatted = [hex_table[b] for b in data]
    
    # Write to stdout using a buffered approach
    out = sys.stdout
    out.write(f"static const unsigned char {var_name}[] = {{\n")
    
    # Join in rows of 12 for readability
    for i in range(0, len(formatted), 12):
        chunk = formatted[i : i + 12]
        out.write("    " + ", ".join(chunk))
        if i + 12 < len(formatted):
            out.write(",\n")
        else:
            out.write("\n")
    
    out.write("};\n\n")
    out.write(f"static const size_t {var_name}_len = {len(data)};\n")

if __name__ == "__main__":
    main()


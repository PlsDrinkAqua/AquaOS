#!/usr/bin/env python3
"""Small 24-bit BMP test card; no external image libraries required."""
import struct
from pathlib import Path

def demo_bmp(top_down=False):
    width, height = 160, 120
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            r, g, b = ((255, 0, 0) if x < width//2 else (0, 255, 0)) if y < height//2 else (
                (0, 0, 255) if x < width//2 else (255, 255, 255))
            row.extend((b, g, r))
        rows.append(bytes(row))
    pixels = b''.join(rows if top_down else reversed(rows))
    return struct.pack('<2sIHHI', b'BM', 54+len(pixels), 0, 0, 54) + struct.pack(
        '<IiiHHIIiiII', 40, width, -height if top_down else height, 1, 24,
        0, len(pixels), 0, 0, 0, 0) + pixels

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_bytes(demo_bmp())

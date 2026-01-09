#!/usr/bin/env python
# SPDX-FileCopyrightText: 2024 Google LLC
# SPDX-License-Identifier: Apache-2.0

"""
Fix font corruption in system_resources pbpack files.

The CRC fix commits created 526-resource pbpack files but corrupted font
resources (version byte reads as 80 instead of 1-3). This script copies
valid font resources from the original 474-resource files into the 526-resource
files while preserving all other resources.
"""

import sys
import os

# Add tools directory to path for pbpack module
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pbpack import ResourcePack

# Font resource IDs for snowy platform (from resource_ids.auto.h)
FONT_RESOURCE_IDS = [
    7,  # GOTHIC_18_BOLD
    32, 33, 34, 35, 36, 37, 38,  # GOTHIC_09 through GOTHIC_18_EMOJI
    39, 40, 41, 42, 43, 44, 45, 46,  # GOTHIC_24 through GOTHIC_36_BOLD
    47, 48, 49, 50, 51, 52, 53,  # BITHAM fonts
    56, 57, 58, 59, 60, 61, 62, 63,  # DROID_SERIF and LECO fonts
    477,  # FONT_FALLBACK_INTERNAL
    488, 489, 490, 491, 492, 493, 494, 495,  # GOTHIC_EXTENDED fonts
]

def fix_system_resources(original_path, corrupted_path, output_path):
    """
    Fix font corruption by copying font resources from original to corrupted file.

    Args:
        original_path: Path to original 474-resource pbpack with valid fonts
        corrupted_path: Path to 526-resource pbpack with corrupted fonts
        output_path: Path to write fixed 526-resource pbpack
    """
    print(f"Loading original file: {original_path}")
    with open(original_path, 'rb') as f:
        original_pack = ResourcePack.deserialize(f, is_system=True, skip_crc_check=True)

    print(f"Original: {len(original_pack.table_entries)} resources")

    print(f"Loading corrupted file: {corrupted_path}")
    with open(corrupted_path, 'rb') as f:
        corrupted_pack = ResourcePack.deserialize(f, is_system=True)

    print(f"Corrupted: {len(corrupted_pack.table_entries)} resources")

    # Copy font resources from original to corrupted
    fonts_replaced = 0
    for font_id in FONT_RESOURCE_IDS:
        resource_index = font_id - 1  # Resource IDs are 1-based, array is 0-based

        if resource_index >= len(original_pack.table_entries):
            print(f"  Skipping font ID {font_id} (not in original)")
            continue

        if resource_index >= len(corrupted_pack.table_entries):
            print(f"  Skipping font ID {font_id} (not in corrupted)")
            continue

        orig_entry = original_pack.table_entries[resource_index]
        corr_entry = corrupted_pack.table_entries[resource_index]

        # Copy the content from original to corrupted
        orig_content = original_pack.contents[orig_entry.content_index]

        # Update the corrupted pack's content
        corrupted_pack.contents[corr_entry.content_index] = orig_content

        # Update the entry's length and recalculate CRC
        import stm32_crc
        corr_entry.length = len(orig_content)
        corr_entry.crc = stm32_crc.crc32(orig_content)

        fonts_replaced += 1
        print(f"  Replaced font resource ID {font_id}")

    print(f"\nReplaced {fonts_replaced} font resources")

    # Mark as not finalized and reset offsets so finalize() will recalculate them
    corrupted_pack.finalized = False
    for entry in corrupted_pack.table_entries:
        entry.offset = -1
    corrupted_pack.finalize()

    print(f"Writing fixed file: {output_path}")
    with open(output_path, 'wb') as f:
        corrupted_pack.serialize(f)

    print("Done!")

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: fix_font_corruption.py <original_pbpack> <corrupted_pbpack> <output_pbpack>")
        sys.exit(1)

    fix_system_resources(sys.argv[1], sys.argv[2], sys.argv[3])

#!/usr/bin/env python
# SPDX-FileCopyrightText: 2024 Google LLC
# SPDX-License-Identifier: Apache-2.0

"""
Python implementation of the legacy defective checksum used by PebbleOS resource storage.

This emulates the behavior of the CRC peripheral in the STM32F2/F4 series MCUs and the bugs
in the legacy CRC driver implementation.

The CRC lookup table was generated from the C implementation in src/fw/util/legacy_checksum.c
"""

# CRC lookup table (nybble-wide)
LOOKUP_TABLE = [
    0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
    0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
    0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
    0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
]


def crc_byte(crc, input_byte):
    """Process a single byte through the CRC."""
    crc = (crc << 4) ^ LOOKUP_TABLE[((crc >> 28) ^ (input_byte >> 4)) & 0x0f]
    crc = (crc << 4) ^ LOOKUP_TABLE[((crc >> 28) ^ (input_byte >> 0)) & 0x0f]
    return crc & 0xFFFFFFFF  # Mask to 32 bits


def legacy_defective_checksum(data):
    """
    Calculate the legacy defective checksum for the given data.

    Args:
        data: bytes object to calculate checksum for

    Returns:
        uint32_t checksum value
    """
    reg = 0xffffffff
    accumulator = [0, 0, 0]
    accumulated_length = 0

    data_bytes = data
    length = len(data_bytes)
    idx = 0

    # Process accumulated bytes from previous calls (not used here but part of the algorithm)
    if accumulated_length:
        while accumulated_length < 3 and length:
            accumulator[accumulated_length] = data_bytes[idx]
            accumulated_length += 1
            idx += 1
            length -= 1

        if accumulated_length == 3 and length:
            reg = crc_byte(reg, data_bytes[idx])
            idx += 1
            length -= 1
            reg = crc_byte(reg, accumulator[2])
            reg = crc_byte(reg, accumulator[1])
            reg = crc_byte(reg, accumulator[0])
            accumulated_length = 0

    # Process 4 bytes at a time in reverse order
    while length >= 4:
        reg = crc_byte(reg, data_bytes[idx + 3])
        reg = crc_byte(reg, data_bytes[idx + 2])
        reg = crc_byte(reg, data_bytes[idx + 1])
        reg = crc_byte(reg, data_bytes[idx + 0])
        idx += 4
        length -= 4

    # Accumulate remaining bytes
    while length:
        accumulator[accumulated_length] = data_bytes[idx]
        accumulated_length += 1
        idx += 1
        length -= 1

    # Process final accumulated bytes with padding
    if accumulated_length:
        # CRC the final bytes forwards (reversed relative to the normal checksum)
        # padded on the left(!) with null bytes.
        for _ in range(4 - accumulated_length):
            reg = crc_byte(reg, 0)
        for i in range(accumulated_length):
            reg = crc_byte(reg, accumulator[i])

    return reg

#!/usr/bin/env python3
import struct

DATA_TYPE = "uint16"
STARTING_INDEX = 5
TABLE_INDEX = 0
VALUES = [1, 255, 255, 55, 23, 13, 43, 4] 
OUTPUT_FILE = "valuedump.txt"
MAX_MESSAGE_SIZE = 9

TYPE_MAP = {
    "uint8": [">B", 1, "FF10"],
    "uint16": [">H", 2, "FF11"],
    "uint32": [">I", 4, "FF12"],
    "int8": [">b", 1, "FF13"],
    "int16": [">h", 2, "FF14"],
    "int32": [">i", 4, "FF15"],
    "bool": [">?", 1, "FF18"],
    "float": ["<f", 4, "FF16"],
    "double": ["<d", 8, "FF17"],
}

fmt, type_size, type_header = TYPE_MAP[DATA_TYPE]

values_per_message = (MAX_MESSAGE_SIZE - 5) // type_size

with open(OUTPUT_FILE, "w") as f:
    start_index = STARTING_INDEX

    for i in range(0, len(VALUES), values_per_message):
        chunk = VALUES[i:i + values_per_message]

        header_starting_index = struct.pack(">H", start_index)
        header_array_index = struct.pack(">B", TABLE_INDEX)

        hex_values = []
        hex_values.append(type_header)
        hex_values.append(header_array_index.hex().upper())
        hex_values.append(header_starting_index.hex().upper())

        for v in chunk:
            packed = struct.pack(fmt, v)
            hex_values.append(packed.hex().upper())

        f.write(" ".join(hex_values) + "\n")

        start_index += len(chunk)



    


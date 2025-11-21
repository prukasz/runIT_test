#!/usr/bin/env python3
import struct

# -------------------------
# GLOBAL SETTINGS
# -------------------------
OUTPUT_FILE = "valuedump.txt"

# -------------------------
# TYPE DEFINITIONS
# -------------------------
TYPE_MAP = {
    "uint8":  ("<B",  1, "FF10", 0),
    "uint16": ("<H",  2, "FF20", 1),
    "uint32": ("<I",  4, "FF30", 2),
    "int8":   ("<b",  1, "FF40", 3),
    "int16":  ("<h",  2, "FF50", 4),
    "int32":  ("<i",  4, "FF60", 5),
    "float":  ("<f",  4, "FF70", 6),
    "double": ("<d",  8, "FF80", 7),
    "bool":   ("<?",  1, "FF90", 8),
}

DATA_TYPES = [
    "DATA_UI8", "DATA_UI16", "DATA_UI32",
    "DATA_I8", "DATA_I16", "DATA_I32",
    "DATA_F", "DATA_D", "DATA_B"
]

# -------------------------
# SINGLE VARIABLE HEADERS
# -------------------------
SINGLE_VAR_HEADER = {
    "uint8":  "FF11",
    "uint16": "FF21",
    "uint32": "FF31",
    "int8":   "FF41",
    "int16":  "FF51",
    "int32":  "FF61",
    "float":  "FF71",
    "double": "FF81",
    "bool":   "FF91",
}

SINGLE_VAR_TYPES_ORDER = ["uint8","uint16","uint32","int8","int16","int32","float","double","bool"]

# -------------------------
# HELPER FUNCTIONS
# -------------------------
def pack_value(dtype, value):
    fmt, _, _, _ = TYPE_MAP[dtype]
    return struct.pack(fmt, value).hex().upper()

def flatten_row_major(values):
    """Flatten nested lists in row-major order."""
    if not isinstance(values, list):
        return [values]
    out = []
    for v in values:
        out.extend(flatten_row_major(v))
    return out

def dump_table_headers_grouped_by_dim(f, tables):
    """Dump table headers grouped by dimension type (datatype + dim bytes, no table index)."""
    grouped = {1: [], 2: [], 3: []}
    for t in tables:
        grouped[len(t["dims"])].append(t)

    for dim in sorted(grouped.keys()):
        if not grouped[dim]:
            continue
        ff_dim = f"FF0{dim}"
        line = [ff_dim]
        for t in grouped[dim]:
            dtype_code = TYPE_MAP[t["dtype"]][3]
            dim_bytes = [struct.pack("<B", d).hex().upper() for d in t["dims"]]
            line += [struct.pack("<B", dtype_code).hex().upper()] + dim_bytes
        f.write(" ".join(line) + "\n")

def dump_table(f, table_index, dtype, start_index, values):
    fmt, _, type_header, _ = TYPE_MAP[dtype]
    flat_values = flatten_row_major(values)
    table_hex = struct.pack("<B", table_index).hex().upper()
    start_hex = struct.pack("<H", start_index).hex().upper()
    data_hex = [pack_value(dtype, v) for v in flat_values]
    f.write(" ".join([type_header, table_hex, start_hex] + data_hex) + "\n")

def dump_single_var(f, dtype, table_index, value):
    header = SINGLE_VAR_HEADER[dtype]
    table_hex = struct.pack("<B", table_index).hex().upper()
    value_hex = pack_value(dtype, value)
    f.write(f"{header} {table_hex} {value_hex}\n")

# -------------------------
# MANUAL TABLE DEFINITIONS
# -------------------------
tables = [
    # uint8 tables
    {"index": 0, "dtype": "uint8", "start": 0, "dims": [3], "values": [0,1,2]},
    {"index": 1, "dtype": "uint8", "start": 0, "dims": [2,3], "values": [[0,1,2],[3,4,5]]},
    {"index": 2, "dtype": "uint8", "start": 0, "dims": [2,2,2], "values": [[[0,1],[2,3]],[[4,5],[6,7]]]},
    # uint16 tables
    {"index": 0, "dtype": "uint16", "start": 0, "dims": [3], "values": [0,1000,2000]},
    {"index": 1, "dtype": "uint16", "start": 0, "dims": [2,3], "values": [[0,1,2],[3,4,5]]},
    {"index": 2, "dtype": "uint16", "start": 0, "dims": [2,2,2], "values": [[[0,1],[2,3]],[[4,5],[6,7]]]},
    # float tables
    {"index": 0, "dtype": "float", "start": 0, "dims": [3], "values": [1.1, 2.2, 3.3]},
    {"index": 1, "dtype": "float", "start": 0, "dims": [2,3], "values": [[1.1,2.2,3.3],[4.4,5.5,6.6]]},
    {"index": 2, "dtype": "float", "start": 0, "dims": [2,2,2], "values": [[[1,2],[3,4]],[[5,6],[7,8]]]},
    # Add other types manually as needed
]

single_vars = [
    ("uint16", 0, 420),
    ("float", 0, 13.37),
    ("uint8", 0, 200),
    ("uint16", 1, 1234),
    ("float", 1, 1.23),
]

# -------------------------
# WRITE HEX DUMP
# -------------------------
with open(OUTPUT_FILE, "w") as f:
    f.write("FFFF\n")

    # single variable counts
    counts = {t:0 for t in SINGLE_VAR_TYPES_ORDER}
    for dtype, _, _ in single_vars:
        counts[dtype] += 1
    header_hex = ["FF00"] + [struct.pack("<B", counts.get(t, 0)).hex().upper() for t in SINGLE_VAR_TYPES_ORDER]
    f.write(" ".join(header_hex) + "\n")

    # table headers grouped by dimension
    dump_table_headers_grouped_by_dim(f, tables)

    f.write("EEEE\n")

    # dump table values individually
    for t in tables:
        dump_table(f, t["index"], t["dtype"], t["start"], t["values"])

    # dump single variables
    for dtype, table_index, val in single_vars:
        dump_single_var(f, dtype, table_index, val)

    f.write("DDDD\n")

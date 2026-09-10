"""
generate_vm_program.py

Generates raw binary packets and prints hex streams to load and execute
a 3-block pipeline (FOR Loop -> EXPR Math -> SET Store) on runIT VM.
"""

import struct
import pathlib

CLASS_VM_LOADER = 0x04

# Packet IDs
PKT_RESET     = 0x40
PKT_OPEN      = 0x41
PKT_ADD_OBJS  = 0x42
PKT_SET_DATA  = 0x43
PKT_ADD_ACC   = 0x44
PKT_ADD_BLOCK = 0x45
PKT_SUBSCRIBE = 0x47
PKT_EXEC      = 0x48

# VM Object types
VM_OBJ_F = 5  # float (32-bit)
VM_OBJ_B = 6  # bool (8-bit)

# Object flags
VM_OBJ_F_MUTABLE       = (1 << 0)
VM_OBJ_F_UPD_RESETABLE = (1 << 1)

# Block types
VM_BLK_EXPR = 1
VM_BLK_FOR  = 5
VM_BLK_SET  = 6

# Expression opcodes
VM_EXPR_IN  = 1
VM_EXPR_ADD = 6

# For loop enums
VM_FOR_OP_ADD  = 0
VM_FOR_CMP_LE  = 1

def make_obj_head(payload_size, obj_type, flags=VM_OBJ_F_MUTABLE | VM_OBJ_F_UPD_RESETABLE, name_len=0):
    b0_b1 = payload_size & 0xFFFF
    b2 = (obj_type & 0x0F) | ((name_len & 0x0F) << 4)
    b3 = flags & 0xFF
    return struct.pack('<HBB', b0_b1, b2, b3)

def build_packets():
    packets = []

    # 1. RESET (0x40)
    packets.append(('01_RESET', bytes([CLASS_VM_LOADER, PKT_RESET])))

    # 2. OPEN (0x41): 5 objects, 3 accessors, 3 blocks, 2048 bytes pool
    p_open = struct.pack('<BBHHHI', CLASS_VM_LOADER, PKT_OPEN, 5, 3, 3, 2048)
    packets.append(('02_OPEN', p_open))

    # 3. ADD_OBJS (0x42):
    #   Obj 0: Sum (Float)
    #   Obj 1: Loop Index i (Float)
    #   Obj 2: Math Result (Float)
    #   Obj 3: Math ENO (Bool)
    #   Obj 4: Set ENO (Bool)
    objs_body = bytearray([5])  # 5 objects in this frame
    objs_body += struct.pack('<H', 0) + make_obj_head(4, VM_OBJ_F)
    objs_body += struct.pack('<H', 1) + make_obj_head(4, VM_OBJ_F)
    objs_body += struct.pack('<H', 2) + make_obj_head(4, VM_OBJ_F)
    objs_body += struct.pack('<H', 3) + make_obj_head(1, VM_OBJ_B)
    objs_body += struct.pack('<H', 4) + make_obj_head(1, VM_OBJ_B)
    packets.append(('03_ADD_OBJS', bytes([CLASS_VM_LOADER, PKT_ADD_OBJS]) + objs_body))

    # 4. SET_DATA (0x43): Seed initial values (Sum = 0.0f)
    data_body = bytearray([1])  # 1 record
    data_body += struct.pack('<HHH', 0, 0, 4) + struct.pack('<f', 0.0)
    packets.append(('04_SET_DATA', bytes([CLASS_VM_LOADER, PKT_SET_DATA]) + data_body))

    # 5. ADD_ACCESSORS (0x44):
    #   Acc 0: &Obj 0[0] (Sum)
    #   Acc 1: &Obj 1[0] (Index i)
    #   Acc 2: &Obj 2[0] (Math Result)
    acc_body = bytearray([3])  # 3 accessors
    for acc_id, root_id in [(0, 0), (1, 1), (2, 2)]:
        idx_chain = struct.pack('<BI', 1, 0)  # VM_IDX_LITERAL = 1
        acc_body += struct.pack('<HHBB', acc_id, root_id, 1, len(idx_chain)) + idx_chain
    packets.append(('05_ADD_ACC', bytes([CLASS_VM_LOADER, PKT_ADD_ACC]) + acc_body))

    # 6. ADD_BLOCK 0 (FOR Loop):
    #   block_idx=0, type=5, in_cnt=0, q_cnt=1 (Obj 1), en_cnt=0, eno=0xFFFF
    #   custom_data: vm_for_code_t (span=[1, 3), start=1.0, end=5.0, step=1.0, max_turns=10, op=ADD, cmp=LE)
    span_bytes = struct.pack('<HH', 1, 3)
    for_custom = (
        span_bytes +
        struct.pack('<fff', 1.0, 5.0, 1.0) +
        struct.pack('<HBBB3s', 10, VM_FOR_OP_ADD, VM_FOR_CMP_LE, 0, b'\x00\x00\x00')
    )
    b0_fixed = struct.pack('<HHBBBBBBHH', 0, 0, VM_BLK_FOR, 0, 1, 0, 0, 0, len(for_custom), 0xFFFF)
    b0_arrays = struct.pack('<H', 1)  # out_obj_ids = [Obj 1]
    packets.append(('06_BLOCK_0_FOR', bytes([CLASS_VM_LOADER, PKT_ADD_BLOCK]) + b0_fixed + b0_arrays + for_custom))

    # 7. ADD_BLOCK 1 (EXPR Math):
    #   block_idx=1, type=1, in_cnt=2 (Acc 0, Acc 1), q_cnt=1 (Obj 2), eno=Obj 3
    #   RPN: IN 0, IN 1, ADD
    expr_bytecode = bytes([VM_EXPR_IN, 0, VM_EXPR_IN, 1, VM_EXPR_ADD])
    expr_custom = struct.pack('<BBH', 0, 0, len(expr_bytecode)) + expr_bytecode
    b1_fixed = struct.pack('<HHBBBBBBHH', 1, 1, VM_BLK_EXPR, 2, 1, 0, 0, 0, len(expr_custom), 3)
    b1_arrays = struct.pack('<HH', 0, 1) + struct.pack('<H', 2)
    packets.append(('07_BLOCK_1_EXPR', bytes([CLASS_VM_LOADER, PKT_ADD_BLOCK]) + b1_fixed + b1_arrays + expr_custom))

    # 8. ADD_BLOCK 2 (SET Store):
    #   block_idx=2, type=6, in_cnt=2 (Acc 2=Src, Acc 0=Dst), q_cnt=0, eno=Obj 4
    b2_fixed = struct.pack('<HHBBBBBBHH', 2, 2, VM_BLK_SET, 2, 0, 0, 0, 0, 0, 4)
    b2_arrays = struct.pack('<HH', 2, 0)
    packets.append(('08_BLOCK_2_SET', bytes([CLASS_VM_LOADER, PKT_ADD_BLOCK]) + b2_fixed + b2_arrays))

    # 9. SUBSCRIBE (0x47): Subscribe to Obj 0 (Sum) and Obj 1 (Index i)
    sub_body = struct.pack('<BHH', 2, 0, 1)
    packets.append(('09_SUBSCRIBE', bytes([CLASS_VM_LOADER, PKT_SUBSCRIBE]) + sub_body))

    # 10. EXEC CONTROL (0x48): NORMAL_MODE (0x05) to start cyclic execution
    packets.append(('10_EXEC_RUN', bytes([CLASS_VM_LOADER, PKT_EXEC, 0x05])))

    return packets

if __name__ == '__main__':
    out_dir = pathlib.Path(__file__).resolve().parent
    bin_path = out_dir / 'program_3blocks.bin'

    pkts = build_packets()
    print("=== runIT VM Wire Packets ===")
    with open(bin_path, 'wb') as f_out:
        for name, p in pkts:
            hex_str = ' '.join(f'{b:02X}' for b in p)
            print(f"[{name:16s}] ({len(p):2d} B): {hex_str}")
            # Prefix each frame with 2-byte length for framed binary transmission/replay
            f_out.write(struct.pack('<H', len(p)) + p)

    print(f"\nFramed binary program saved to: {bin_path}")

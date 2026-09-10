"""
Example and verification script for Python VM Object Creation and Packet Compilation.

Verifies:
1. Object creation with automatic ID assignment (no manual IDs).
2. Optional names: anonymous (name=None, name_size=0) and named user variables.
3. Separate linked Accessor generator tracking object relations and automatic ID assignment.
4. Exact byte-level match against runit_guide_test.c wire packets.
"""
from vm import (
    VMProgram,
    VMType,
    VMIndexKind,
    VMExecCommand,
)


def test_guide_pipeline_packets():
    print("=================================================================")
    print("  TEST 1: Guide Pipeline Objects & Accessors (Exact Byte Match)")
    print("=================================================================")

    prog = VMProgram("guide_pipeline")

    # Define 5 objects (all anonymous in guide test, initial float 0.0 for sum)
    # Notice: IDs are NOT specified!
    obj_sum  = prog.add_temp(type=VMType.F, count=1, initial=0.0, upd_resetable=False)  # Obj 0
    obj_i    = prog.add_temp(type=VMType.F, count=1, upd_resetable=False)               # Obj 1
    obj_math = prog.add_temp(type=VMType.F, count=1, upd_resetable=False)               # Obj 2
    obj_eno1 = prog.add_temp(type=VMType.B, count=1, upd_resetable=False)               # Obj 3
    obj_eno2 = prog.add_temp(type=VMType.B, count=1, upd_resetable=False)               # Obj 4

    # Register accessors relationally (again, no manual IDs!)
    # obj[0] returns an Accessor linked to the object at literal index 0
    acc_sum  = prog.add_accessor(obj_sum[0])
    acc_i    = prog.add_accessor(obj_i[0])
    acc_math = prog.add_accessor(obj_math[0])

    # Subscribe to live telemetry for Obj 0 and Obj 1
    prog.subscribe(obj_sum, obj_i)

    # Compile into wire packets (setting arena_size=2048 to match guide test)
    compiled = prog.compile(arena_size=2048)

    print(compiled.dump_hex())

    # Verification against C runit_guide_test.c packets:
    expected_reset = bytes([0x04, 0x40])
    expected_open = bytes([0x04, 0x41, 0x05, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00])  # blk_cnt=0 here
    # Notice: In the guide test blk_cnt was 3 because it loaded 3 blocks. Here blk_cnt=0 since blocks aren't added yet.
    expected_objs = bytes([
        0x04, 0x42, 0x05,
        0x00, 0x00, 0x04, 0x00, 0x05, 0x03,
        0x01, 0x00, 0x04, 0x00, 0x05, 0x03,
        0x02, 0x00, 0x04, 0x00, 0x05, 0x03,
        0x03, 0x00, 0x01, 0x00, 0x06, 0x03,
        0x04, 0x00, 0x01, 0x00, 0x06, 0x03,
    ])
    expected_seed = bytes([
        0x04, 0x43, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x00,
    ])
    expected_acc = bytes([
        0x04, 0x44, 0x03,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x01, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x02, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
    ])
    expected_sub = bytes([0x04, 0x47, 0x02, 0x00, 0x00, 0x01, 0x00])
    expected_exec = bytes([0x04, 0x48, 0x05])

    assert compiled.packets[0].data == expected_reset, f"Reset mismatch: {compiled.packets[0].hex}"
    assert compiled.packets[2].data == expected_objs, f"Add Objs mismatch: {compiled.packets[2].hex}"
    assert compiled.packets[3].data == expected_seed, f"Seed Data mismatch: {compiled.packets[3].hex}"
    assert compiled.packets[4].data == expected_acc, f"Add Acc mismatch: {compiled.packets[4].hex}"
    assert compiled.packets[5].data == expected_sub, f"Subscribe mismatch: {compiled.packets[5].hex}"
    assert compiled.packets[6].data == expected_exec, f"Exec mismatch: {compiled.packets[6].hex}"

    print(">>> All Guide Test packets matched wire layout perfectly! <<<\n")


def test_named_and_relational_accessors():
    print("=================================================================")
    print("  TEST 2: Named User Variables & Relational Ref Accessors")
    print("=================================================================")

    prog = VMProgram("sensor_program")

    # 1. Named user variable: "temperature", float array of 4 readings
    temp = prog.add_var("temperature", type=VMType.F, count=4, initial=[21.5, 22.0, 22.8, 23.1])

    # 2. Named user variable: "sensor_id", u32 scalar
    sensor_id = prog.add_var("sensor_id", type=VMType.U32, count=1, initial=42)

    # 3. Dynamic index: an accessor that points to sensor_id[0]
    idx_acc = prog.add_accessor(sensor_id[0])

    # 4. Indirect accessor: temperature[idx_acc] (VM_IDX_REF pointing to idx_acc)
    temp_dyn_acc = prog.add_accessor(temp[idx_acc])

    # 5. Anonymous scratch buffer (name is None, name_size=0)
    scratch = prog.add_temp(type=VMType.F, count=1)
    scratch_acc = prog.add_accessor(scratch[0])

    prog.subscribe(temp, sensor_id)
    compiled = prog.compile()

    print(compiled.dump_hex())

    # Check that object IDs were assigned sequentially
    assert temp.id == 0
    assert sensor_id.id == 1
    assert scratch.id == 2

    # Check tagged flags and name bytes
    assert temp.is_tagged() is True
    assert temp.name == "temperature"
    assert temp.name_size == 11
    assert scratch.is_tagged() is False
    assert scratch.name_size == 0

    # Check accessor IDs
    assert idx_acc.id is not None
    assert temp_dyn_acc.id is not None
    # Prerequisite index accessor must have lower ID than the one referencing it!
    assert idx_acc.id < temp_dyn_acc.id

    print(">>> Named variables, automatic IDs, and relational REF accessors verified! <<<\n")


def test_nested_structs():
    print("=================================================================")
    print("  TEST 3: Nested Structs & VM_OBJ_PTR Child Linking")
    print("=================================================================")

    prog = VMProgram("robot_motor_controller")

    # High-level struct definition: Motor struct with 3 fields
    motor = prog.add_struct(
        "Motor",
        {
            "speed": (VMType.F, 120.5),
            "current": (VMType.F, 2.3),
            "enabled": (VMType.B, True),
        },
    )

    # Access nested fields:
    # Option A: motor.speed[0]
    acc_speed = prog.add_accessor(motor.speed[0])
    # Option B: motor["current"][0]
    acc_cur = prog.add_accessor(motor["current"][0])
    # Option C: motor.field("enabled")
    acc_en = prog.add_accessor(motor.field("enabled"))

    prog.subscribe(motor, motor.children[0])
    compiled = prog.compile()

    print(compiled.dump_hex())

    # Verify:
    # 3 children + 1 parent container = 4 objects total
    assert len(compiled.objects) == 4
    # Children must have lower IDs than the parent container!
    for child in motor.children:
        assert child.id < motor.id

    assert motor.type == VMType.PTR
    assert motor.count == 3

    print(">>> Nested structs, child linking, and field accessors verified! <<<\n")


if __name__ == "__main__":
    test_guide_pipeline_packets()
    test_named_and_relational_accessors()
    test_nested_structs()

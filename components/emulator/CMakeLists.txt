idf_component_register(
    SRCS 
        "blocks/emulator_blocks.c"
        "core/emulator_interface.c"
        "core/emulator_body.c"
        "core/emulator_loop.c"
        "core/emulator_parse.c"
        "core/emulator_variables.c"
        "core/emulator_errors.c"
        "core/emulator_debug.c"
        "utils/utils_global_access.c"
        "utils/utils_block_in_q_access.c"
        "utils/utils_parse.c"
        "blocks/block_math.c"
        "blocks/block_set_global.c"
        "blocks/block_logic"
        "blocks/block_for"
        "blocks/block_timer"

    INCLUDE_DIRS 
        "blocks/include"
        "core/include"
        "utils/include"

    REQUIRES 
        esp_timer 
        main
)
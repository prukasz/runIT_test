idf_component_register(
    SRCS 
        "blocks/emulator_blocks.c"
        "core/emulator_interface.c"
        "core/emulator_body.c"
        "core/emulator_loop.c"
        "core/emulator_parse.c"
        "core/emulator_variables.c"
        "core/emulator_errors.c"
        "utils/utils_global_access.c"
        "utils/utils_block_in_q_access.c"
        "blocks/block_math.c"
        "blocks/block_global_access.c"

    INCLUDE_DIRS 
        "blocks/include"
        "core/include"
        "utils/include"

    REQUIRES 
        esp_timer 
        main
)
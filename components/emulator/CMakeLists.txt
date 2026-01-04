idf_component_register(
    SRCS 
        "blocks/emulator_blocks.c"
        "blocks/emulator_blocks_functions_list.c"
        "core/emulator_interface.c"
        "core/emulator_body.c"
        "core/emulator_loop.c"
        "core/emulator_parse.c"
        "core/emulator_variables.c"
        "core/emulator_errors.c"
        "core/emulator_variables_acces.c"
        "blocks/block_math.c"
        "blocks/block_logic.c"
        "blocks/block_for.c"
        "blocks/block_timer.c"

    INCLUDE_DIRS 
        "blocks/include"
        "core/include"

    REQUIRES 
        esp_timer 
        main
)
idf_component_register(
    SRCS "emulator_parse.c" "emulator_loop.c" "emulator_variables.c" "emulator.c" "emulator_body.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer main
)
idf_component_register(
    SRCS "emulator_loop.c" "emulator_variables.c" "emulator.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer main
)
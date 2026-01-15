idf_component_register(
    SRCS "esc_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES pca9685 common servo_manager
)
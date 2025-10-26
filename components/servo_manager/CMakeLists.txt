idf_component_register(
    SRCS "servo_manager.c"
    INCLUDE_DIRS "include" 
    REQUIRES driver common pca9685
)
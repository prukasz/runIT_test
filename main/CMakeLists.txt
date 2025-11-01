file(GLOB_RECURSE srcs "main.c" "*.c" "ble/*.c")
idf_component_register(SRCS "${srcs}"
                    PRIV_REQUIRES spi_flash
                    INCLUDE_DIRS "ble/include" "i2c_tasks/include"
                    REQUIRES pca9685 pcf8575 driver ads1115 mpu6050 bt servo_manager esc_manager nvs_flash emulator)

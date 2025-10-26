#include "gpio_manager.h"

gpio_manager_mode_t* gpio_maganger_init(void){
    return (gpio_manager_mode_t*)calloc(30, sizeof(gpio_manager_mode_t));
}

gpio_manager_pca_mode_t* gpio_manager_pca9685_init(void){
    return (gpio_manager_pca_mode_t*)calloc(16, sizeof(gpio_manager_pca_mode_t));
}

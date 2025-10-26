#include "gpio_manager.h"
#define PCA_MAX 16

static gpio_manager_pca_mode_t pcf_gpio_manager[16];
static gpio_manager_mode_t gpio_manager[30];
static const char *TAG  = "GPIO_MANAGER";


gpio_manager_pca_mode_t gpio_manager_check_pca9685(uint8_t channel){
    if (channel>PCA_MAX){
        ESP_LOGE(TAG, " wrong channel %d ", channel);
        return 0xFF;
    }
    else
    {
        return pcf_gpio_manager[channel];
    }
}
void gpio_manager_set_pca9685(uint8_t channel, gpio_manager_pca_mode_t mode){
    if (channel>PCA_MAX){
        ESP_LOGE(TAG, " wrong channel %d ", channel);
        return;
    }
    else
    {
       pcf_gpio_manager[channel] = mode;
    }
}

gpio_manager_mode_t gpio_manager_check(uint8_t gpio){
    if (gpio>PCA_MAX){
        ESP_LOGE(TAG, " wrong channel %d ", gpio);
        return 0;
    }
    if (GPIO_MNG_EMPTY ==  gpio_manager[gpio])
    {return 1;}
    else
    {
        ESP_LOGE(TAG, "channel %d already has role %d", gpio, gpio_manager[gpio]);
        return 0;
    }

}



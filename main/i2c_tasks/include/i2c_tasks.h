#pragma once
#include "pca9685.h"
#include "pcf8575.h"
#include "ads1115.h"
#include "mpu6050.h"
#include "freertos/task.h"
#include "freertos/task.h"
#include "esp_log.h"

void task_pca9685(void *parameters);
void task_ads1115(void *parameters);
void task_mpu6050(void *parameters);

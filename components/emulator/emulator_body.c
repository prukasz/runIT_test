#include "emulator.h"
#include "emulator_loop.h"
#include "emulator_variables.h"

static const char *TAG = "BODY TASK";
extern SemaphoreHandle_t loop_semaphore;
extern SemaphoreHandle_t wtd_semaphore;
#define TABLE_SIZE 3
#define ITERATIONS 500000
/*main block logic*/
void loop_task(void* params){
    while(1){
        if(pdTRUE == xSemaphoreTake(loop_semaphore, portMAX_DELAY)) {
            int64_t start_time = esp_timer_get_time();
            
            for(int i = 0; i < TABLE_SIZE; i++){
                for(int j = 0; j < TABLE_SIZE; j++){
                    MEM_GET_U8(0, i, j, SIZE_MAX);
                    ESP_LOGI(TAG, "table i %d j %d vaj %d", i ,j ,MEM_GET_U8(0, i, j, SIZE_MAX));
                }
            }
            ESP_LOGI(TAG, "SINGLE1 %d",MEM_GET_U8(0, SIZE_MAX, SIZE_MAX, SIZE_MAX));
            ESP_LOGI(TAG, "SINGL2 %d",MEM_GET_U8(1, SIZE_MAX, SIZE_MAX, SIZE_MAX));
            /*
                HERE
            LOOP
            LOGIC
            EXECUTION
            */
            int64_t end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "loop completed in %.3f ms, watchdog triggered: %d", (end_time - start_time) / 1000.0, status.wtd.wtd_triggered);
            xSemaphoreGive(wtd_semaphore);
            taskYIELD();
        }
    }
}
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Debug execution macro.
 * Encapsulates any arbitrary code fragment (e.g. DBG(ESP_LOGI(...)); or DBG(my_var++;))
 * Can be enabled globally with #define ENABLE_DBG 1 or per-file with #define LOCAL_DBG 1.
 * When disabled, compiler dead-code elimination removes the code and strings with zero overhead.
 */
#ifndef ENABLE_DBG
#define ENABLE_DBG 0
#endif

/*
 * DBG(...) macro:
 * Call DBG(code) to execute code only when debug is active.
 * To enable per file, place:
 *   #undef LOCAL_DBG
 *   #define LOCAL_DBG 1
 * in that .c file.
 */
#ifndef LOCAL_DBG
#define LOCAL_DBG 0
#endif

#define DBG(...)                                              \
  do {                                                        \
    if ((ENABLE_DBG) || (LOCAL_DBG)) {                        \
      __VA_ARGS__;                                            \
    }                                                         \
  } while (0)

/**
 * @brief Creates a static ESP-IDF Ringbuffer.
 */
#define R_RINGBUFFER_DEFINE(name, buffer_size, type)                                             \
  static uint8_t _##name##_storage[(buffer_size)] __attribute__((aligned(4)));                   \
  static StaticRingbuffer_t _##name##_struct;                                                    \
  RingbufHandle_t name = NULL;                                                                   \
  __attribute__((constructor)) static void _init_##name(void) {                                  \
    name = xRingbufferCreateStatic((buffer_size), (type), _##name##_storage, &_##name##_struct); \
  }

/**
 * @brief Creates a static Event Group.
 */
#define R_EVENT_GROUP_DEFINE(name)                              \
  static StaticEventGroup_t _##name##_buffer;                   \
  EventGroupHandle_t name = NULL;                               \
  __attribute__((constructor)) static void _init_##name(void) { \
    name = xEventGroupCreateStatic(&_##name##_buffer);          \
  }

/**
 * @brief Creates a static Mutex.
 */
#define R_MUTEX_DEFINE(name)                                    \
  static StaticSemaphore_t _##name##_buffer;                    \
  SemaphoreHandle_t name = NULL;                                \
  __attribute__((constructor)) static void _init_##name(void) { \
    name = xSemaphoreCreateMutexStatic(&_##name##_buffer);      \
  }

#define R_RECURSIVE_MUTEX_DEFINE(name)                          \
  static StaticSemaphore_t _##name##_buffer;                    \
  SemaphoreHandle_t name = NULL;                                \
  __attribute__((constructor)) static void _init_##name(void) { \
    name = xSemaphoreCreateRecursiveMutexStatic(&_##name##_buffer); \
  }

/**
 * @brief Creates a static Binary Semaphore.
 */
#define R_BINARY_SEM_DEFINE(name)                               \
  static StaticSemaphore_t _##name##_buffer;                    \
  SemaphoreHandle_t name = NULL;                                \
  __attribute__((constructor)) static void _init_##name(void) { \
    name = xSemaphoreCreateBinaryStatic(&_##name##_buffer);     \
  }

/**
 * @brief Creates a static Counting Semaphore.
 */
#define R_COUNTING_SEM_DEFINE(name, max_count, initial_count)                               \
  static StaticSemaphore_t _##name##_buffer;                                                \
  SemaphoreHandle_t name = NULL;                                                            \
  __attribute__((constructor)) static void _init_##name(void) {                             \
    name = xSemaphoreCreateCountingStatic((max_count), (initial_count), &_##name##_buffer); \
  }

/**
 * @brief Creates a static Queue.
 */
#define R_QUEUE_DEFINE(name, length, item_size)                                             \
  static uint8_t _##name##_storage[(length) * (item_size)] __attribute__((aligned(4)));     \
  static StaticQueue_t _##name##_buffer;                                                    \
  QueueHandle_t name = NULL;                                                                \
  __attribute__((constructor)) static void _init_##name(void) {                             \
    name = xQueueCreateStatic((length), (item_size), _##name##_storage, &_##name##_buffer); \
  }

/**
 * @brief Creates a static Stream Buffer.
 */
#define R_STREAM_BUFFER_DEFINE(name, buffer_size, trigger_level)                                            \
  static uint8_t _##name##_storage[(buffer_size)] __attribute__((aligned(4)));                              \
  static StaticStreamBuffer_t _##name##_buffer;                                                             \
  StreamBufferHandle_t name = NULL;                                                                         \
  __attribute__((constructor)) static void _init_##name(void) {                                             \
    name = xStreamBufferCreateStatic((buffer_size), (trigger_level), _##name##_storage, &_##name##_buffer); \
  }

/**
 * @brief Creates a static Message Buffer.
 */
#define R_MESSAGE_BUFFER_DEFINE(name, buffer_size)                                          \
  static uint8_t _##name##_storage[(buffer_size)] __attribute__((aligned(4)));              \
  static StaticMessageBuffer_t _##name##_buffer;                                            \
  MessageBufferHandle_t name = NULL;                                                        \
  __attribute__((constructor)) static void _init_##name(void) {                             \
    name = xMessageBufferCreateStatic((buffer_size), _##name##_storage, &_##name##_buffer); \
  }

#define R_TASK_DEFINE(name, stack_words)             \
  static StackType_t _##name##_stack[(stack_words)]; \
  static StaticTask_t _##name##_tcb;                 \
  TaskHandle_t name = NULL

/**
 * @brief Starts a statically allocated task explicitly.
 */
#define R_TASK_START(name, task_func, param, priority)                                                                                                   \
  do {                                                                                                                                                   \
    name = xTaskCreateStatic((task_func), #name, (sizeof(_##name##_stack) / sizeof(StackType_t)), (param), (priority), _##name##_stack, &_##name##_tcb); \
  } while (0)

#define R_TASK_START_ON_CORE(name, task_func, param, priority, core_id)                                                                                                         \
  do {                                                                                                                                                                          \
    name = xTaskCreateStaticPinnedToCore((task_func), #name, (sizeof(_##name##_stack) / sizeof(StackType_t)), (param), (priority), _##name##_stack, &_##name##_tcb, (core_id)); \
  } while (0)

#define R_EVENT_SET(group, bits) xEventGroupSetBits((group), (bits))

#define R_EVENT_CLEAR(group, bits) xEventGroupClearBits((group), (bits))

/**
 * @brief Waits for ANY of the specified bits to be set, then clears them.
 * @usage if (R_EVENT_AWAIT_ANY(rik_events, BIT_WIFI | BIT_BLE, WAIT_FOREVER)) { ... }
 */
#define R_EVENT_AWAIT_ANY(group, bits, timeout_ticks) xEventGroupWaitBits((group), (bits), pdTRUE, pdFALSE, (timeout_ticks))

/**
 * @brief Waits for ALL of the specified bits to be set simultaneously, then clears them.
 */
#define R_EVENT_AWAIT_ALL(group, bits, timeout_ticks) xEventGroupWaitBits((group), (bits), pdTRUE, pdTRUE, (timeout_ticks))

#define SECONDS(sec) pdMS_TO_TICKS((sec) * 1000)
#define MSEC(ms) pdMS_TO_TICKS(ms)
#define WAIT_FOREVER portMAX_DELAY
#define NO_WAIT 0

#ifndef SE_IS_OK
#define SE_IS_OK(x) ((x) == pdPASS || (x) == pdTRUE)
#endif

/**
 * @brief Sends a 32-bit value directly to a task, waking it up.
 */
#define R_NOTIFY_SEND(task_handle, value) xTaskNotify((task_handle), (value), eSetValueWithOverwrite)

#define R_NOTIFY_SEND_ISR(task_handle, value, pxHigherPriorityTaskWoken) xTaskNotifyFromISR((task_handle), (value), eSetValueWithOverwrite, (pxHigherPriorityTaskWoken))

/**
 * @brief Waits for a notification. Clears the value on exit.
 * @param timeout_ticks How long to wait in FreeRTOS ticks.
 * @param out_val_ptr Pointer to a uint32_t to store the received value.
 * @return pdTRUE if received, pdFALSE if timed out.
 */
#define R_NOTIFY_AWAIT(timeout_ticks, out_val_ptr) xTaskNotifyWait(0x00, ULONG_MAX, (out_val_ptr), (timeout_ticks))

#define R_TIMER_DEFINE(name, period_ticks, auto_reload, callback_func)                                                            \
  static StaticTimer_t _##name##_buffer;                                                                                          \
  TimerHandle_t name = NULL;                                                                                                      \
  __attribute__((constructor)) static void _init_##name(void) {                                                                   \
    name = xTimerCreateStatic(#name, (period_ticks), (auto_reload) ? pdTRUE : pdFALSE, NULL, (callback_func), &_##name##_buffer); \
  }

#define R_TIMER_START(name) xTimerStart(name, 0)
#define R_TIMER_STOP(name) xTimerStop(name, 0)
#define R_TIMER_RESET(name) xTimerReset(name, 0)
#define R_TIMER_START_ISR(name, px) xTimerStartFromISR(name, px)

/**
 * @brief Locks a mutex/semaphore. Returns true if successful, false if timed out.
 * @usage if ( R_MUTEX_LOCK(i2c_bus, MSEC(100)) ) { ... }
 */
#define R_MUTEX_LOCK(mutex_handle, timeout_ticks) (xSemaphoreTake((mutex_handle), (timeout_ticks)))
#define R_RECURSIVE_MUTEX_LOCK(mutex_handle, timeout_ticks) (xSemaphoreTakeRecursive((mutex_handle), (timeout_ticks)))

/**
 * @brief Unlocks a mutex/semaphore.
 * @usage R_MUTEX_UNLOCK(i2c_bus);
 */
#define R_MUTEX_UNLOCK(mutex_handle) xSemaphoreGive((mutex_handle))
#define R_RECURSIVE_MUTEX_UNLOCK(mutex_handle) xSemaphoreGiveRecursive((mutex_handle))

/**
 * @brief Gives a semaphore from an ISR and triggers a context switch if needed.
 */
#define R_SEM_GIVE_ISR(sem_handle)                \
  do {                                            \
    BaseType_t _woken = pdFALSE;                  \
    xSemaphoreGiveFromISR((sem_handle), &_woken); \
    if (_woken) {                                 \
      portYIELD_FROM_ISR();                       \
    }                                             \
  } while (0)

#define R_QUEUE_SEND(queue_handle, item_ptr, timeout_ticks) (xQueueSend((queue_handle), (item_ptr), (timeout_ticks)))

/**
 * @brief Pushes an item to a queue from an ISR and yields if a context switch is required.
 */
#define R_QUEUE_SEND_ISR(queue_handle, item_ptr)      \
  do {                                                \
    BaseType_t _woken = pdFALSE;                      \
    xQueueSendFromISR((queue_handle), (item_ptr), &_woken); \
    if (_woken) {                                     \
      portYIELD_FROM_ISR();                           \
    }                                                 \
  } while (0)

/**
 * @brief Pulls an item from a queue. Returns true if data was received.
 * @usage if ( R_QUEUE_RECEIVE(sensor_queue, &my_data, MSEC(500)) ) { ... }
 */
#define R_QUEUE_RECEIVE(queue_handle, item_ptr, timeout_ticks) (xQueueReceive((queue_handle), (item_ptr), (timeout_ticks)))

/**
 * @brief Looks at the next item in the queue WITHOUT removing it.
 */
#define R_QUEUE_PEEK(queue_handle, item_ptr, timeout_ticks) (xQueuePeek((queue_handle), (item_ptr), (timeout_ticks)))

#ifdef __cplusplus
}
#endif

/**
 * @brief Dodaje element na koniec listy. (O(N))
 */
#define LL_APPEND(head, add)          \
  do {                                \
    (add)->next = NULL;               \
    if ((head) == NULL) {             \
      (head) = (add);                 \
    } else {                          \
      __typeof__(head) _tmp = (head); \
      while (_tmp->next != NULL) {    \
        _tmp = _tmp->next;            \
      }                               \
      _tmp->next = (add);             \
    }                                 \
  } while (0)

/**
 * @brief Dodaje element na początek listy. (O(1))
 */
#define LL_PREPEND(head, add) \
  do {                        \
    (add)->next = (head);     \
    (head) = (add);           \
  } while (0)

/**
 * @brief Usuwa wskazany element z listy. (O(N))
 */
#define LL_DELETE(head, del)                        \
  do {                                              \
    if ((head) == (del)) {                          \
      (head) = (head)->next;                        \
    } else {                                        \
      __typeof__(head) _tmp = (head);               \
      while (_tmp != NULL && _tmp->next != (del)) { \
        _tmp = _tmp->next;                          \
      }                                             \
      if (_tmp != NULL) {                           \
        _tmp->next = (del)->next;                   \
      }                                             \
    }                                               \
  } while (0)

/**
 * @brief Szybka iteracja po liście (Tylko do odczytu/modyfikacji danych).
 * UWAGA: Nie używaj tego, jeśli wewnątrz pętli robisz LL_DELETE lub free(el)!
 */
#define LL_FOREACH(head, el) for ((el) = (head); (el) != NULL; (el) = (el)->next)

/**
 * @brief Bezpieczna iteracja po liście (Zaprojektowana do usuwania elementów).
 * Używa wskaźnika tymczasowego, aby nie zgubić ścieżki po zrobieniu free(el).
 */
#define LL_FOREACH_SAFE(head, el, tmp) for ((el) = (head), (tmp) = ((head) != NULL ? (head)->next : NULL); (el) != NULL; (el) = (tmp), (tmp) = ((el) != NULL ? (el)->next : NULL))

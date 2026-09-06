#pragma once
#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"

// Owners for the sys_errors component itself (config + telemetry plumbing).
#define SYS_ERRORS_OWNER_MAP(X)                             \
  X(OWNER_SYS_ERRORS_BASE, 0xA800, "OWNER_SYS_ERRORS_BASE") \
  X(OWNER_SYS_ERRORS_CONFIG, 0xA801, "OWNER_SYS_ERRORS_CONFIG")

#define SYS_ERROR_BASE_MAP(X)                           \
  X(ERR_NO_HANDLE, struct { uint8_t unused; })          \
  X(ERR_NULL_PTR, struct { uint8_t unused; })           \
  X(                                                    \
      ERR_INVALID_VAL_UI32, struct {                    \
        uint32_t val;                                   \
        uint32_t min;                                   \
        uint32_t max;                                   \
      })                                                \
  X(                                                    \
      ERR_INVALID_VAL_I32, struct {                     \
        int32_t val;                                    \
        int32_t min;                                    \
        int32_t max;                                    \
      })                                                \
  X(                                                    \
      ERR_INVALID_VAL_F, struct {                       \
        float val;                                      \
        float min;                                      \
        float max;                                      \
      })                                                \
  X(ERR_DEV_DEP_FAILED, struct { uint8_t dev_id; })     \
  X(ERR_DEP_FAILED, struct { uint8_t unused; })         \
  X(ERR_ESP_ERR, struct { esp_err_t esp_code; })        \
  X(ERR_BASE_NO_MEM, struct { uint8_t unused; })        \
  X(ERR_BASE_NOT_SUPPORTED, struct { uint8_t unused; }) \
  X(ERR_BASE_NOT_FOUND, struct { uint8_t unused; })     \
  X(ERR_BASE_INVALID_STATE, struct { uint8_t unused; })

/**
 * @brief Human-readable descriptions for base tags - see SE_describe_payload() in sys_error.h.
 */
#define SYS_ERROR_BASE_LOGGER_MAP(X) \
  X(ERR_NO_HANDLE)                   \
  X(ERR_NULL_PTR)                    \
  X(ERR_INVALID_VAL_UI32)            \
  X(ERR_INVALID_VAL_I32)             \
  X(ERR_INVALID_VAL_F)               \
  X(ERR_DEV_DEP_FAILED)              \
  X(ERR_DEP_FAILED)                  \
  X(ERR_ESP_ERR)                     \
  X(ERR_BASE_NO_MEM)                 \
  X(ERR_BASE_NOT_SUPPORTED)          \
  X(ERR_BASE_NOT_FOUND)              \
  X(ERR_BASE_INVALID_STATE)

#define LOG_BODY_ERR_NO_HANDLE(p, out, out_size) \
  do {                                           \
    (void)(p);                                   \
    snprintf((out), (out_size), "no handle");    \
  } while (0)
#define LOG_BODY_ERR_NULL_PTR(p, out, out_size)             \
  do {                                                      \
    (void)(p);                                              \
    snprintf((out), (out_size), "unexpected NULL pointer"); \
  } while (0)
#define LOG_BODY_ERR_INVALID_VAL_UI32(p, out, out_size) snprintf((out), (out_size), "value %lu out of range [%lu, %lu]", (unsigned long)(p)->val, (unsigned long)(p)->min, (unsigned long)(p)->max)
#define LOG_BODY_ERR_INVALID_VAL_I32(p, out, out_size) snprintf((out), (out_size), "value %ld out of range [%ld, %ld]", (long)(p)->val, (long)(p)->min, (long)(p)->max)
#define LOG_BODY_ERR_INVALID_VAL_F(p, out, out_size) snprintf((out), (out_size), "value %g out of range [%g, %g]", (double)(p)->val, (double)(p)->min, (double)(p)->max)
#define LOG_BODY_ERR_DEV_DEP_FAILED(p, out, out_size) snprintf((out), (out_size), "device %u dependency failed", (p)->dev_id)
#define LOG_BODY_ERR_DEP_FAILED(p, out, out_size)      \
  do {                                                 \
    (void)(p);                                         \
    snprintf((out), (out_size), "nested call failed"); \
  } while (0)
#define LOG_BODY_ERR_ESP_ERR(p, out, out_size) snprintf((out), (out_size), "ESP-IDF error %s (0x%x)", esp_err_to_name((p)->esp_code), (p)->esp_code)
#define LOG_BODY_ERR_BASE_NO_MEM(p, out, out_size) \
  do {                                             \
    (void)(p);                                     \
    snprintf((out), (out_size), "out of memory");  \
  } while (0)
#define LOG_BODY_ERR_BASE_NOT_SUPPORTED(p, out, out_size)   \
  do {                                                      \
    (void)(p);                                              \
    snprintf((out), (out_size), "operation not supported"); \
  } while (0)
#define LOG_BODY_ERR_BASE_NOT_FOUND(p, out, out_size) \
  do {                                                \
    (void)(p);                                        \
    snprintf((out), (out_size), "not found");         \
  } while (0)
#define LOG_BODY_ERR_BASE_INVALID_STATE(p, out, out_size)            \
  do {                                                               \
    (void)(p);                                                       \
    snprintf((out), (out_size), "invalid state for this operation"); \
  } while (0)

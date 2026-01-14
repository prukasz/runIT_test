#pragma once

/**************************************************************************************************
 * This header contains configuration macros for emulator logging system
 * Enabling/disabling logging features and setting queue sizes and task stack size
 * ENABLE_LOGGING - enables logging to ESP log system
 * ENABLE_DEBUG_LOGS - enables extra debug logs (to be used with ENABLE_LOGGING)
 * ENABLE_REPORT - enables reporting to queues (also enables debug if enabled)
 *************************************************************************************************/

#define ENABLE_LOGGING 
//#define ENABLE_DEBUG_LOGS
#define ENABLE_REPORT  
#define LOG_QUEUE_SIZE 64
#define REPORT_QUEUE_SIZE 64
#define LOGGER_TASK_STACK 4096
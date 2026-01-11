#pragma once

#define ENABLE_LOGGING  //logging from queues
#define ENABLE_DEBUG_LOGS //extra logs
#define ENABLE_REPORT  //report to queues (+ debug if enabled)
#define LOG_QUEUE_SIZE 64
#define REPORT_QUEUE_SIZE 64
#define LOGGER_TASK_STACK 4096
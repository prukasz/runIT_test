#pragma once

/**************************************************************************************************
 * This header contains configuration macros for emulator logging system
 IMPORTANT: 
    * - Disabling logging/reporting will save some memory and CPU time but will make debugging and error tracking much harder
    
 *************************************************************************************************/

#define ENABLE_LOG_E
#define ENABLE_LOG_W
#define ENABLE_LOG_I
#define ENABLE_LOG_D

#define ENABLE_ERROR_BUFF//adds errors to error queue
#define ENABLE_STATUS_BUFF //adds reports to report queue

#define ENABLE_LOG_X_FROM_ERROR_MACROS //enable LOG_X from error macros
#define ENABLE_LOG_X_FROM_STATUS_MACROS //enable LOG_X from log macros

#define ENABLE_SENDING_LOGS

#define LOG_QUEUE_SIZE 128
#define REPORT_QUEUE_SIZE 128
#define LOGGER_TASK_STACK 4096

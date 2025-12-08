#pragma once 
#include "stdint.h"
#include "emulator_errors.h"
#include "emulator_types.h"
#define MAX_ARR_DIMS 3

/**
 * @brief struct for handling passed by user for global varaible access 
 * @note !this is very memory hungry we will need to consider other approach! 
 */
typedef struct _global_acces_t{
    struct _global_acces_t* next0;  //next reference if NULL get from custom indices
    struct _global_acces_t* next1;  //next reference if NULL get from custom indices
    struct _global_acces_t* next2;  //next reference if NULL get from custom indices 
    uint8_t target_type;  //type of variable accesed in this struct
    uint8_t target_idx;  //index of variable accesed in this struct
    uint8_t target_custom_indices[MAX_ARR_DIMS];  //static custom indices, FF tells dims of arrays
}_global_acces_t;


/**
 * @brief retrive value form memory in recursive way 
 */
emu_err_t utils_global_var_acces_recursive(_global_acces_t *root, double *out);



#pragma once 
#include "stdint.h"
#include "emulator_errors.h"
#include "emulator_types.h"
#include "gatt_buff.h"

#define MAX_ARR_DIMS 3

/*circular include fix*/
struct _block_handle_s;
typedef struct _block_handle_s block_handle_t;

/**
 * @brief struct for handling passed by user for global varaible access 
 * @note !this is very memory hungry we will need to consider other approach! 
 */
typedef struct global_acces_t{
    struct global_acces_t* next0;  //next reference if NULL get from custom indices
    struct global_acces_t* next1;  //next reference if NULL get from custom indices
    struct global_acces_t* next2;  //next reference if NULL get from custom indices 
    uint8_t target_type;  //type of variable accesed in this struct
    uint8_t target_idx;  //index of variable accesed in this struct
    uint8_t target_custom_indices[MAX_ARR_DIMS];  //static custom indices, FF tells dims of arrays
}global_acces_t;


/**
 * @brief retrive value form memory in recursive way 
 * @param root Start node that has references to other
 * @param out Output variable pointer
 * @return 
 */
emu_err_t utils_global_var_acces_recursive(global_acces_t *root, double *out);


/**
 * @brief Free block global variable access list
 * @param access_list Block access list
 * @param cnt Total roots count  
 */
void utils_global_var_access_free(global_acces_t** access_list, uint8_t cnt);


/**
 * @brief Parse all provided global access references for provided block
 * @param source Data source
 * @param start Search start index 
 * @param block Block to assign to 
 */
emu_err_t utils_parse_global_access_for_block(chr_msg_buffer_t *source, uint16_t start, block_handle_t *block);

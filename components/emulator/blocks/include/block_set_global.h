#pragma once
#include "emulator_variables_acces.h"


/****************************************************************************
                    SET BLOCK
                ________________
    -->VAL  [0]|            BOOL|[0]ENO     -->
    -->IDX  [1]|OPT(U8)         |
    -->IDX  [2]|OPT(U8) SET     |
    -->IDX  [3]|OPT(U8)         |
               |________________|
 
****************************************************************************/



/**
 * @brief This block enable to set selected global variable provided in global reference,
 *  if this block has inputs [1,2,3] then will try to use their values as indices instead of
 *  static provided in global reference, if there is only input 0 -> Double, then block will set value 
 *  fetching all from global references provded
 */
emu_result_t block_set_global(block_handle_t* block_data);


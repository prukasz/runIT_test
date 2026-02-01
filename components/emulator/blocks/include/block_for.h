#pragma once 
#include "emulator_blocks.h"
#include "emulator_logging.h"



/****************************************************************************
                    FOR BLOCK
                ________________
    -->EN   [0]|BOOL        BOOL|[0]ENO     -->
    -->START[1]|[OPT]           |[1]ITERATOR-->
    -->STOP [2]|[OPT]   FOR     |
    -->STEP [3]|[OPT]           |
               |                |
               |                |
               |________________|
 
****************************************************************************
DESCRIPTION:
This block executes chain of related blocks connected to it's input. The FOR expression consist of:
INPUTS:
-EN enable
-START Start value 
-STOP stop value
-STEP operation step 
Comparisions (iterator vs STOP)
FOR_COND_GT:       = 0x01, // >
FOR_COND_LT:       = 0x02, // <
FOR_COND_GTE:      = 0x04, // >=
FOR_COND_LTE:      = 0x05, // <=
Operators (iterator operator STEP)
FOR_OP_ADD        = 0x01,
FOR_OP_SUB        = 0x02,
FOR_OP_MUL        = 0x03,
FOR_OP_DIV        = 0x04,

NOTE:
Start, Stop, Step can be hardcoded
Operator is hardcoded

OUTPUTS:
ENO = EN state - here connect blocks that has to run in for loop
ITERATOR  - "i"

EXAMPLE:
["TABLE"][10] = 10*[0]
FOR (START =0 , STOP  = 10, STEP = 1, OP = ADD, COND = LT)

["Start"]->[EN][FOR]                                     [SET]("TABLE"["index"])
               [FOR][ITERATOR]--------------------->[VAL][SET]
                                |----->["index"]

Equivalent to:
static int TABLE[10];
static int index;
for(int i = 0; i< 10; i++){
    index = i;
    TABLE[index]=i;
}


*****************************************************************************/


emu_result_t block_for(block_handle_t block);

emu_result_t block_for_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block);
void block_for_free(block_handle_t block);
emu_result_t block_for_verify(block_handle_t block);
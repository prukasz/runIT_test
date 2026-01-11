#pragma once 

typedef enum {
    /********PARSER ORDERS **************/
    ORD_CREATE_CONTEXT        = 0xFFFF,  //Create context with provided size
    ORD_PARSE_VARIABLES       = 0xEEEE,  //Parse variables types and arrays sizes
    ORD_PARSE_VARIABLES_DATA  = 0xDDDD,  //Fill created variables with provided data
    ORD_EMU_CREATE_BLOCK_LIST = 0xb100,  //Create list for number of provided blocks (Total blocks in code)
    ORD_EMU_CREATE_BLOCKS     = 0xb200,  //Create blocks (Inputs, Outputs, Type, Custom data)
    ORD_CHECK_CODE            = 0x0030,  //Check code completeness before start (once after parsing finish)

    /********RESET ORDERS  ***************/
    ORD_RESET_ALL             = 0x0001,  //Brings emulator to startup state, provides way to eaisly send new code
    ORD_RESET_BLOCKS          = 0x0002,  //Reset all blocks and theirs data
    ORD_RESET_MGS_BUF         = 0x0003,  //Clear msg buffer
    
    /********LOOP CONTROL****************/
    ORD_EMU_LOOP_START     = 0x1000, //start loop / resume 
    ORD_EMU_LOOP_STOP      = 0x2000, //stop loop aka pause
    ORD_EMU_LOOP_INIT      = 0x3000, //init loop first time 

    /********DEBUG OPTIONS **************/
    ORD_EMU_INIT_WITH_DBG  = 0x3333, //init loop with debug
    ORD_EMU_SET_PERIOD     = 0x4000, //change period of loop
    ORD_EMU_RUN_ONCE       = 0x5000, //Run one cycle and wait for next order
    ORD_EMU_RUN_WITH_DEBUG = 0x6000, //Run with debug (Dump after each cycle)
    ORD_EMU_RUN_ONE_STEP   = 0x7000, //Run one block / one step (With debug)

}emu_order_t;
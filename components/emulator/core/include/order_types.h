#pragma once 

/*****************************************************************************************************************
 * Orders are provided to emulator as 2byte uint16_t codes via provoded queue and automatically processesed in order they are received
 * Remeber that some orders require buffer clearance or specific messages to be sent before they can be processed
 * Some orders should be provided in specific states only (like loop stopped/started etc.) or in specific order (like parsing orders)
 * Orders can denied if state is not correct or some other conditions are not met (like missing context etc.)
 * ******************************************************************************************************************/


typedef enum {
    /********PARSER ORDERS **************/
    ORD_PARSE_CONTEXT_CFG        = 0xAAF0,  /*Parse and create context*/
    ORD_PARSE_VARIABLES          = 0xAAF1,  //Parse and create variables instances 
    ORD_PARSE_VARIABLES_S_DATA   = 0xAAFA,  //Fill created scalar variables with data
    ORD_PARSE_VARIABLES_ARR_DATA = 0xAAFB, 

    ORD_PARSE_LOOP_CFG           = 0xAAA0,     //Create loop with provided config
    ORD_PARSE_CODE_CFG           = 0xAAAA,  //Create code context with provided config (block list)
    ORD_PARSE_ACCESS_CFG         = 0xAAAB,  //Create access instances storage

    ORD_PARSE_BLOCK_HEADER       = 0xAAB0,
    ORD_PARSE_BLOCK_INPUTS       = 0xAAB1,
    ORD_PARSE_BLOCK_OUTPUTS      = 0xAAB2,
    ORD_PARSE_BLOCK_DATA         = 0xAABA,

    ORD_PARSE_RESET_STATUS       = 0xAA00, //Reset parser status to initial state (for new code parsing)
    ORD_PARSE_SUBSCRIPTION_INIT  = 0xAAC0, //Initialize subscription system with provided config
    ORD_PARSE_SUBSCRIPTION_ADD   = 0xAAC1, //Add subscription

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

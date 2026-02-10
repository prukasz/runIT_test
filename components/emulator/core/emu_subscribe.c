#include "emu_subscribe.h"

#define TAG __FILE_NAME__
static emu_subscribe_buff_t* subscription_list;
static uint16_t subs_nex_idx = 0;
static uint16_t subs_count = 0;


/**
 * @brief Each value tells how many can pack in one buff 
 */
static uint8_t* packing_list;


static uint8_t buff[512];

#undef OWNER
#define OWNER EMU_OWNER_emu_subscribe_init
emu_result_t emu_subscribe_init(const uint16_t count){
    //create space for list
    subscription_list = (emu_subscribe_buff_t*)calloc(count, sizeof(emu_subscribe_buff_t));
    if(!subscription_list){
        RET_E(EMU_ERR_NO_MEM, "Failed to allocate subscription list for %d items", count);
    }
    //
    packing_list = (uint8_t*)calloc(count, sizeof(uint8_t));
    if(!packing_list){
        free(subscription_list);
        RET_E(EMU_ERR_NO_MEM, "Failed to allocate packing list for %d items", count);
    }
    subs_count = count;
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_EMU_OWNER_emu_subscribe_register
emu_result_t emu_subscribe_register(const mem_access_t* what, const uint16_t index){
    
    if(subs_nex_idx >= subs_count){RET_E(EMU_ERR_SUBSCRIPTION_FULL, "Subscription list is full, cannot register more items");}

    subscription_list[subs_nex_idx].instance = what;
    //one element subscirbed, Only support for non recursive
    subscription_list[subs_nex_idx].self_index = index;
    if(!what->whole_array){
        subscription_list[subs_nex_idx].data_size_in_bytes = MEM_TYPE_SIZES[what->instance->type];
    }else{
        
    }

    subscription_list[subs_nex_idx].context = what->instance->context;
    subscription_list[subs_nex_idx].type = what->instance->type;
    subscription_list[subs_nex_idx].updated = what->instance->updated;
}



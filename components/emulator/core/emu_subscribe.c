#include "emu_subscribe.h"
#include "emu_parse.h"
#include "emu_helpers.h"
#include "mem_types.h"

#define TAG __FILE_NAME__

extern __attribute__((aligned(32))) mem_context_t mem_contexts[];

typedef struct{
    struct{
        uint16_t inst_idx; 
        uint8_t context   : 3;  /*Context that isnstance shall belong to*/
        uint8_t type      : 4;  /*mem_types_t type*/
        uint8_t updated   : 1;  /*Updated flag can be used for block output variables*/
    }head;
    //info 
    uint16_t el_cnt;
    void *data;
}pub_instance_t;

struct
{
    uint8_t buff[512]; //space for packet 

    pub_instance_t *sub_list;
    size_t sub_list_max_size;
    size_t next_free_sub_idx;

    uint8_t *pub_pack;
    size_t pub_pack_size;
    size_t pub_pack_max_size;
} config;

#undef OWNER
#define OWNER EMU_OWNER_emu_subscribe_parse_init
emu_result_t emu_subscribe_parse_init(const uint8_t *packet_data, const uint16_t packet_len, void* custom){
    LOG_I(TAG, "Initializing subscription system with packet size: %d", packet_len);
    uint16_t sub_list_size = parse_get_u16(packet_data, 0);
    config.sub_list_max_size = sub_list_size;
    config.sub_list = (pub_instance_t *)calloc(sub_list_size, sizeof(pub_instance_t));

    config.pub_pack_max_size =sub_list_size;
    config.pub_pack = (uint8_t *)calloc(config.pub_pack_max_size, sizeof(uint8_t));

    config.next_free_sub_idx = 0;
    config.pub_pack_size = 0;
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_emu_subscribe_parse_register
emu_result_t emu_subscribe_parse_register(const uint8_t *packet_data, const uint16_t packet_len, void* custom){

    LOG_I(TAG, "Parsing subscription registration packet, size: %d", packet_len);
    uint8_t ctx = packet_data[0];
    uint8_t count = packet_data[1];
    if(packet_len < 2 + count * 3) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");

    const uint8_t *payload = &packet_data[2];
    uint16_t payload_len = packet_len - 2;

    for(int i = 0; i < count; i++){
        uint8_t type = payload[0];
        uint16_t inst_idx = parse_get_u16(payload, 1);

        if (config.next_free_sub_idx >= config.sub_list_max_size) RET_E(EMU_ERR_SUBSCRIPTION_FULL, "Subscription list is full");
        
        uint8_t el_cnt = 1;
        for(int j = 0; j < mem_contexts[ctx].types[type].instances[inst_idx].dims_cnt; j++){
            el_cnt *= mem_contexts[ctx].types[type].dims_pool[mem_contexts[ctx].types[type].instances[inst_idx].dims_idx + j];
        }

        config.sub_list[config.next_free_sub_idx].head.inst_idx = inst_idx;
        config.sub_list[config.next_free_sub_idx].head.context = ctx;
        config.sub_list[config.next_free_sub_idx].head.type = type;
        config.sub_list[config.next_free_sub_idx].head.updated = mem_contexts[ctx].types[type].instances[inst_idx].updated;
        config.sub_list[config.next_free_sub_idx].el_cnt = el_cnt; 
        config.sub_list[config.next_free_sub_idx].data = mem_contexts[ctx].types[type].instances[inst_idx].data.raw;
    
        payload += 3;
        payload_len -= 3;
        config.next_free_sub_idx++;
    }
    emu_subscribe_process();
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_emu_subscribe_process
emu_result_t emu_subscribe_process()
{
    LOG_I(TAG, "Processing subscriptions, total: %d", config.next_free_sub_idx);
    int packet = 0;
    memset(config.pub_pack, 0, config.pub_pack_max_size);
    for(int i = 0; i < config.next_free_sub_idx; i++){

        size_t total_size = 0;
        while(total_size < 511 && i < config.next_free_sub_idx)
        {
            size_t next_size  = sizeof(((pub_instance_t *)0)->head) + config.sub_list[i].el_cnt * MEM_TYPE_SIZES[config.sub_list[i].head.type];
            if(next_size>511){
                REP_W(EMU_LOG_to_large_to_sub, "Instance data to large for single packet %"PRIu16"", config.sub_list[i].el_cnt);
            }
            total_size += next_size;
            if (total_size < 511){
                config.pub_pack[packet]++;
                i++;
            }
            else{
                packet++;
                break;
            }
        }
    }
    config.pub_pack_size = packet+1;
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_emu_subscribe_send
emu_result_t emu_subscribe_send(){
    
    LOG_I(TAG, "Sending subscription data, total packets: %d", config.pub_pack_size);
    uint16_t offset = 1;
    uint16_t instance = 0;

    for(int packet = 0; packet < config.pub_pack_size; packet++){
        config.buff[0] = PACKET_H_PUBLISH;
        while(instance < config.pub_pack[packet]){ //config.pub_pack[0] - ilosc pakietow do wyslania
            LOG_I(TAG, "HUJ");
            memcpy(config.buff+offset, &config.sub_list[instance].head, sizeof(((pub_instance_t *)0)->head));
            offset+= sizeof(((pub_instance_t *)0)->head);
            memcpy(config.buff+offset, config.sub_list[instance].data, config.sub_list[instance].el_cnt * MEM_TYPE_SIZES[config.sub_list[instance].head.type]);
            offset+= config.sub_list[instance].el_cnt * MEM_TYPE_SIZES[config.sub_list[instance].head.type];
            instance++;
        }
        //send buff with offset length
        ESP_LOG_BUFFER_HEX(TAG, config.buff, offset);
        //
        offset = 0;
        instance = 0;
    }   
    return EMU_RESULT_OK();
}


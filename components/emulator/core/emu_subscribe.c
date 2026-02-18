#include "emu_subscribe.h"
#include "emu_parse.h"
#include "emu_helpers.h"
#include "mem_types.h"
#include "emu_types_info.h"
#include "gatt_svc.h"

#define TAG __FILE_NAME__
#define PKT_BUFF_SIZE 512


extern __attribute__((aligned(32))) mem_context_t mem_contexts[];

typedef struct pub_instance_t{
    /**
     * @brief Instnace header + data == 1 instance data
     */
    struct{
        uint16_t inst_idx;      /*Instance index in selected context*/
        uint8_t context   : 3;  /*Context that isnstance shall belong to*/
        uint8_t type      : 4;  /*mem_types_t type*/
        uint8_t updated   : 1;  /*Updated flag can be used for block output variables*/
    }head;
    void *data; //instance data pointer

    uint16_t el_cnt; //;for fast data copy, no dims iteration during sending

}pub_instance_t;

struct{
    uint8_t packet_buff[PKT_BUFF_SIZE]; //packet to send 

    /**
     * @brief List of all subscribed instances, filled during registration, then used for sending
     */
    pub_instance_t *sub_list;

    /**
     * @brief Index of next free slot in subscription list, also total count of subscribed instances
     */
    size_t next_free_sub_idx;
    /**
     * @brief Total size of subscription list 
     */
    size_t sub_list_max_size;


    /**
     * @brief Used for caching group of instances to each packet 
     * @details Each packet has precounted instaces count, then while sending just copy next N packets to buff directly, no size check 
     */
    uint8_t *pub_pack;
    /**
     * @brief Total packest to send 
     */
    size_t pub_pack_size;
    /**
     * @brief Total packets possbile to send (1 packet -> 1 instance)
     */
    size_t pub_pack_max_size;
}sub_manager_t;

#undef OWNER
#define OWNER EMU_OWNER_emu_subscribe_parse_init
emu_result_t emu_subscribe_parse_init(const uint8_t *packet_data, const uint16_t packet_len, void* nothing){

    uint16_t sub_list_size = parse_get_u16(packet_data, 0);
    sub_manager_t.sub_list_max_size = sub_list_size;
    sub_manager_t.sub_list = (pub_instance_t *)calloc(sub_list_size, sizeof(pub_instance_t));

    sub_manager_t.pub_pack_max_size =sub_list_size;
    sub_manager_t.pub_pack = (uint8_t *)calloc(sub_manager_t.pub_pack_max_size, sizeof(uint8_t));

    sub_manager_t.next_free_sub_idx = 0;
    sub_manager_t.pub_pack_size = 0;
    RET_OK("Initialized with max size: %"PRIu16"", sub_list_size);
}

#undef OWNER
#define OWNER EMU_OWNER_emu_subscribe_parse_register
emu_result_t emu_subscribe_parse_register(const uint8_t *packet_data, const uint16_t packet_len, void* custom){

    uint8_t ctx = packet_data[0];
    uint8_t count = packet_data[1];
    if(packet_len < 2 + count * 3) RET_E(EMU_ERR_PACKET_INCOMPLETE, "Packet too short");

    const uint8_t *payload = &packet_data[2];
    uint16_t payload_len = packet_len - 2;

    for(int i = 0; i < count; i++){
        uint8_t type = payload[0];
        uint16_t inst_idx = parse_get_u16(payload, 1);

        if (sub_manager_t.next_free_sub_idx >= sub_manager_t.sub_list_max_size) RET_E(EMU_ERR_SUBSCRIPTION_FULL, "Subscription list is full");
        
        uint8_t el_cnt = 1;
        for(int j = 0; j < mem_contexts[ctx].types[type].instances[inst_idx].dims_cnt; j++){
            el_cnt *= mem_contexts[ctx].types[type].dims_pool[mem_contexts[ctx].types[type].instances[inst_idx].dims_idx + j];
        }

        sub_manager_t.sub_list[sub_manager_t.next_free_sub_idx].head.inst_idx = inst_idx;
        sub_manager_t.sub_list[sub_manager_t.next_free_sub_idx].head.context = ctx;
        sub_manager_t.sub_list[sub_manager_t.next_free_sub_idx].head.type = type;
        sub_manager_t.sub_list[sub_manager_t.next_free_sub_idx].head.updated = mem_contexts[ctx].types[type].instances[inst_idx].updated;
        sub_manager_t.sub_list[sub_manager_t.next_free_sub_idx].el_cnt = el_cnt; 
        sub_manager_t.sub_list[sub_manager_t.next_free_sub_idx].data = mem_contexts[ctx].types[type].instances[inst_idx].data.raw;
    
        payload += 3;
        payload_len -= 3;
        sub_manager_t.next_free_sub_idx++;
        LOG_I(TAG, "Registered subscription: ctx: %"PRIu8", type: %s, inst_idx: %"PRIu16", el_cnt: %"PRIu16"", ctx, MEM_TYPES_TO_STR[type], inst_idx, el_cnt);
    }
    emu_result_t res = emu_subscribe_process();
    if(res.code != EMU_OK) {RET_WD(res.code, 0xFFFF, ++res.depth ,"Processing failed, %s", EMU_ERR_TO_STR(res.code));}
    RET_OK("Registered %"PRIu16"instances", count);
}

#undef OWNER
#define OWNER EMU_OWNER_emu_subscribe_process
emu_result_t emu_subscribe_process()
{
    int packet = 0;
    memset(sub_manager_t.pub_pack, 0, sub_manager_t.pub_pack_max_size);
    for(int i = 0; i < sub_manager_t.next_free_sub_idx; i++){

        size_t total_size = 0;
        while(total_size < PKT_BUFF_SIZE-1 && i < sub_manager_t.next_free_sub_idx)
        {
            size_t next_size  = sizeof(((pub_instance_t *)0)->head) + sizeof(uint16_t) + sub_manager_t.sub_list[i].el_cnt * MEM_TYPE_SIZES[sub_manager_t.sub_list[i].head.type];
            if(next_size> PKT_BUFF_SIZE-1) {
                REP_W(EMU_LOG_to_large_to_sub, "Instance data to large for single packet %"PRIu16"", sub_manager_t.sub_list[i].el_cnt);
            }
            total_size += next_size;
            if (total_size < PKT_BUFF_SIZE-1){
                sub_manager_t.pub_pack[packet]++;
                i++;
            }
            else{
                packet++;
                break;
            }
        }
    }
    sub_manager_t.pub_pack_size = packet+1;
    return EMU_RESULT_OK();
}

#undef OWNER
#define OWNER EMU_OWNER_emu_subscribe_send
emu_result_t emu_subscribe_send(){
    
    uint16_t offset = 1;
    uint16_t instance = 0;

    for(int packet = 0; packet < sub_manager_t.pub_pack_size; packet++){
        sub_manager_t.packet_buff[0] = PACKET_H_PUBLISH;
        while(instance < sub_manager_t.pub_pack[packet]){ //config.pub_pack[0] - ilosc pakietow do wyslania
            memcpy(sub_manager_t.packet_buff+offset, &sub_manager_t.sub_list[instance].head, sizeof(((pub_instance_t *)0)->head));
            offset+= sizeof(((pub_instance_t *)0)->head);
            memcpy(sub_manager_t.packet_buff+offset, &sub_manager_t.sub_list[instance].el_cnt, sizeof(uint16_t));
            offset+= sizeof(uint16_t);
            memcpy(sub_manager_t.packet_buff+offset, sub_manager_t.sub_list[instance].data, sub_manager_t.sub_list[instance].el_cnt * MEM_TYPE_SIZES[sub_manager_t.sub_list[instance].head.type]);
            offset+= sub_manager_t.sub_list[instance].el_cnt * MEM_TYPE_SIZES[sub_manager_t.sub_list[instance].head.type];
            instance++;
        }
        //send buff with offset length
        // ESP_LOG_BUFFER_HEX(TAG, sub_manager_t.packet_buff, offset);
        gatt_send_notify(sub_manager_t.packet_buff, offset);
        //

        offset = 0;
        instance = 0;
    }   
    RET_OK("Sent %"PRIu16" packets", sub_manager_t.pub_pack_size);
}


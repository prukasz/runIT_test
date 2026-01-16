#include "emulator_variables.h"
#include <string.h>

static const char *TAG = "EMU_VARIABLES";


/********************************************************************************************************** *
 * 
 *                           GLOBAL MEMORY CONTEXTS
 * 
 * *************************************************************************************************************/

emu_mem_t* s_mem_contexts[MAX_MEM_CONTEXTS] = {NULL};

// Internal Helper (just retrieve memory context for code clarity)
emu_mem_t* _get_mem_context(uint8_t id) {
    if (id >= MAX_MEM_CONTEXTS) return NULL;
    return s_mem_contexts[id];
}

//Register (aka add context)
static emu_result_t emu_mem_register_context(uint8_t id, emu_mem_t *mem_ptr) {
    if (id < MAX_MEM_CONTEXTS) {
        if (s_mem_contexts[id]) {EMU_RETURN_CRITICAL(EMU_ERR_INVALID_ARG, EMU_OWNER_emu_mem_register_context, 0, 0, TAG, "Context ID %d already registered", id);}
        s_mem_contexts[id] = mem_ptr;
        EMU_RETURN_OK(EMU_LOG_context_registered, EMU_OWNER_emu_mem_register_context, 0,  TAG, "Context ID %d registered", id);
    }
    EMU_RETURN_CRITICAL(EMU_ERR_INVALID_ARG, EMU_OWNER_emu_mem_register_context, 0, 0, TAG, "Context ID %d out of bounds", id);
}

//free context (selected)
static void emu_mem_free_context(uint8_t id) {
        emu_mem_t *mem = _get_mem_context(id);
        if (mem){
            for (uint8_t i = 0; i < TYPES_COUNT; i++) {
                if (mem->data_pools[i]) { free(mem->data_pools[i]); mem->data_pools[i] = NULL; }
                if (mem->instances[i])  { free(mem->instances[i]);  mem->instances[i] = NULL; }
                if (mem->instances_arenas[i])     { free(mem->instances_arenas[i]);     mem->instances_arenas[i] = NULL; }
                mem->instances_cnt[i] = 0;
            }
        }
    EMU_REPORT(EMU_LOG_context_freed, EMU_OWNER_emu_mem_free_contexts, 0, TAG, "Context ID %d freed", id);
}



void emu_mem_free_contexts() {
    for (uint8_t i = 0; i < MAX_MEM_CONTEXTS; i++) {
        emu_mem_free_context(i); //just iterate thruoug all possible, free_context will decide
    }
}



emu_result_t emu_mem_alloc_context(uint8_t context_id, 
                                   uint16_t var_counts[TYPES_COUNT], 
                                   uint16_t inst_counts[TYPES_COUNT], 
                                   uint16_t total_dims[TYPES_COUNT]) 
{
    emu_result_t res;

    //first we check if context exist
    emu_mem_t *mem = _get_mem_context(context_id);
    
    if (!mem) { //create if already not created
        mem = calloc(1, sizeof(emu_mem_t));
        if (!mem) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_CONTEXT, EMU_OWNER_emu_mem_alloc_context, 0, 0, TAG, "Context struct alloc failed for ID %d", context_id);}
        res =emu_mem_register_context(context_id, mem);
        if (res.code != EMU_OK) {EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_mem_alloc_context,0,  ++res.depth, TAG, "Context struct alloc failed for ID %d", context_id);}
    }else{
        EMU_RETURN_CRITICAL(EMU_ERR_MEM_ALREADY_CREATED, EMU_OWNER_emu_mem_alloc_context, 0, 0, TAG, "Context already created" );
    }

    //then we allocate data pools with provided size
    for (int i = 0; i < TYPES_COUNT; i++) {
        // 1. Data Pools
        if (var_counts[i] > 0) {
            mem->data_pools[i] = calloc(var_counts[i], DATA_TYPE_SIZES[i]);
            if (mem->data_pools[i] == NULL) {
                emu_mem_free_context(context_id);
                EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_mem_alloc_context, 0, 0, TAG, "Data pool alloc failed type %d count %d, context freed", i, var_counts[i]);
            }
        }
        
        //and also we create instaces space for scalars and array
        uint16_t cnt = inst_counts[i];
        mem->instances_cnt[i] = cnt;

        if (cnt > 0) {
            mem->instances[i] = calloc(cnt, sizeof(void*));   
            if (mem->instances[i] == NULL) {
                emu_mem_free_context(context_id);
                EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_mem_alloc_context, 0, 0, TAG, "Instance table alloc failed type %d count %d, context freed", i, cnt);
            }
            
            //total size is standard scalar and space for each dimension as it require extra byte to store them (value)
            size_t arena_size = (cnt * sizeof(emu_mem_instance_s_t)) + total_dims[i];

            mem->instances_arenas[i] = calloc(1, arena_size);
            if (mem->instances_arenas[i] == NULL) {
                emu_mem_free_context(context_id);
                EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_emu_mem_alloc_context, 0, 0, TAG, "Instance arena alloc failed type %d size %d, context freed", i, (int)arena_size);
            }
            ESP_LOGI(TAG, "Ctx %d Type %d: Allocated (Vars:%d, Inst:%d, Arena:%d B)", context_id, i, var_counts[i], cnt, (int)arena_size);
        }
    }
    EMU_RETURN_OK(EMU_LOG_context_allocated, EMU_OWNER_emu_mem_alloc_context, 0,TAG, "Context %d allocated successfully.", context_id);
}



emu_result_t emu_mem_parse_create_context(chr_msg_buffer_t *source) {
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);  //as usual we get message from buffer
    
    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        
        if (len < 3) continue;  //check if not empty or order (len 2)

        uint16_t header = 0;
        memcpy(&header, data, 2);  //here we get specific header

        if (header == VAR_H_SIZES) {  //we check if header match 
            uint8_t ctx_id = data[2];  //context id (mem type)
            
            size_t payload_size = 3 * TYPES_COUNT * sizeof(uint16_t);   //we need to have complete packet 
            if (len != (3 + payload_size)) {  //header + id = 3
                EMU_RETURN_WARN(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_emu_mem_parse_create_context, 0 , 0, TAG, "Packet incomplete for VAR_H_SIZES (len=%d, expected=%d)", (int)len, (int)(3 + payload_size));
            }

            uint16_t idx = 3;  //skip header then just copy
            
            uint16_t var_counts[TYPES_COUNT];
            memcpy(var_counts, &data[idx], sizeof(var_counts)); idx += sizeof(var_counts);
            
            uint16_t inst_counts[TYPES_COUNT];
            memcpy(inst_counts, &data[idx], sizeof(inst_counts)); idx += sizeof(inst_counts);
            
            uint16_t extra_dims[TYPES_COUNT];
            memcpy(extra_dims, &data[idx], sizeof(extra_dims));
            
            //create context based on fetched data by parser
            emu_result_t res = emu_mem_alloc_context(ctx_id, var_counts, inst_counts, extra_dims);
            if (res.code != EMU_OK) {EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_mem_parse_create_context, 0, ++res.depth, TAG, "Context alloc failed for ID %d", ctx_id);}
            EMU_RETURN_OK(EMU_LOG_scalars_created, EMU_OWNER_emu_mem_parse_create_context, 0, TAG, "Context %d created from VAR_H_SIZES", ctx_id);
        }
    }
    EMU_RETURN_WARN(EMU_ERR_PACKET_NOT_FOUND, EMU_OWNER_emu_mem_parse_create_context, 0 , 0, TAG, "Packet not found for VAR_H_SIZES");
}

emu_result_t emu_mem_parse_create_scalar_instances(chr_msg_buffer_t *source) {
    uint8_t *data;
    size_t packet_len;
    size_t buff_size = chr_msg_buffer_size(source);
    
    const size_t EXPECTED_PAYLOAD = TYPES_COUNT * sizeof(uint16_t); 

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &packet_len);
        
        //empty packet or order
        if (packet_len < 3) continue;

        uint16_t header = 0;
        memcpy(&header, data, 2);

        if (header == VAR_H_SCALAR_CNT) {
            uint8_t ctx_id = data[2];
            
            //get right context 
            emu_mem_t *mem = _get_mem_context(ctx_id);
            if (unlikely(!mem)) {EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_CONTEXT, EMU_OWNER_emu_mem_parse_create_scalar_instances, 0, 0, TAG, "Scalar Cnt: Ctx %d not found", ctx_id);}
            
            //validate packet len
            uint16_t idx = 3; 
            if (packet_len != (idx + EXPECTED_PAYLOAD)) {EMU_RETURN_WARN(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_emu_mem_parse_create_scalar_instances, 0, 0, TAG, "Packet incomplete for VAR_H_SCALAR_CNT");}

            //iterate every datatype
            for (uint8_t type = 0; type < TYPES_COUNT; type++) {
                uint16_t scalar_cnt = 0;
                memcpy(&scalar_cnt, &data[idx], sizeof(uint16_t));
                idx += sizeof(uint16_t);
                
                if (scalar_cnt == 0) continue; 

                //check for any nulls
                if (unlikely(!mem->instances_arenas[type] || !mem->instances[type] || !mem->data_pools[type])) {
                    EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_INSTANCE, EMU_OWNER_emu_mem_parse_create_scalar_instances, 0, 0, TAG, "Missing allocations for Type %d Ctx %d", type, ctx_id);
                }

                
                uint8_t *arena_base = (uint8_t*)mem->instances_arenas[type];
                void **instance_table = mem->instances[type];
                uint8_t *data_base = (uint8_t*)mem->data_pools[type];

                //intance creation
                for (uint16_t k = 0; k < scalar_cnt; k++) {
                    //we get offset from previous parse
                    size_t arena_off = mem->instances_arenas_offset[type];
                    emu_mem_instance_s_t *meta = (emu_mem_instance_s_t*)(arena_base + arena_off);
                    
                    size_t data_idx = mem->next_data_pools_idx[type];
                    //calculate pointer    
                    void *data_ptr = data_base + (data_idx * DATA_TYPE_SIZES[type]);

                    //fill struct
                    meta->data = data_ptr;          
                    meta->dims_cnt = 0;           // Skalar ma 0 wymiarów
                    meta->target_type = type;
                    meta->context_id = ctx_id;
                    meta->updated = 1;
                    meta->_reserved = 0;

                    // D. Zapisz wskaźnik do instancji w tabeli lookup (dla szybkiego dostępu po ID)
                    size_t inst_idx = mem->next_instance_idx[type];
                    instance_table[inst_idx] = meta;

                    // E. Aktualizacja liczników (Offsety)
                    mem->instances_arenas_offset[type] += sizeof(emu_mem_instance_s_t);
                    mem->next_data_pools_idx[type]++;
                    mem->next_instance_idx[type]++;
                }
                ESP_LOGI(TAG, "Ctx %d: Created %d SCALARS of Type %s", ctx_id, scalar_cnt, EMU_DATATYPE_TO_STR[type]);
            } 
            EMU_RETURN_OK(EMU_LOG_scalars_created, EMU_OWNER_emu_mem_parse_create_scalar_instances, 0, TAG, "Scalars created for context %d", ctx_id);
        }
    }
    EMU_RETURN_WARN(EMU_ERR_PACKET_NOT_FOUND, EMU_OWNER_emu_mem_parse_create_scalar_instances, 0, 0, TAG, "Packet not found for VAR_H_SCALAR_CNT");
}

emu_result_t emu_mem_parse_create_array_instances(chr_msg_buffer_t *source) {
    uint8_t *data;       
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);
        
        if (len < 3) continue;

        uint16_t header = 0;
        memcpy(&header, data, 2);

        if (header == VAR_H_ARR) { 
            uint8_t ctx_id = data[2];
            emu_mem_t *mem = _get_mem_context(ctx_id);
            
            if (unlikely(!mem)) {
                EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_CONTEXT, EMU_OWNER_emu_mem_parse_create_array_instances, 0, 0, TAG, "Ctx %d not found", ctx_id);
            }

            uint16_t idx = 3; 

            while (idx < len) {
                // Sprawdzenie końca pakietu (nagłówek instancji to 2 bajty: dims + type)
                if ((idx + 2) > len) break;  

                uint8_t dims_cnt    = data[idx++]; 
                uint8_t target_type = data[idx++]; 

                // Walidacja danych wejściowych
                if (unlikely(target_type >= TYPES_COUNT || dims_cnt > 4)) {
                    EMU_RETURN_CRITICAL(EMU_ERR_INVALID_ARG, EMU_OWNER_emu_mem_parse_create_array_instances, 0, 0, TAG, "Invalid type %d or dims %d", target_type, dims_cnt);
                }

                // Sprawdzenie czy pakiet zawiera wymiary
                if (unlikely((idx + dims_cnt) > len)) {
                    EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_emu_mem_parse_create_array_instances, 0, 0, TAG, "Packet incomplete for array type %d", target_type);
                }

                // Sprawdzenie alokacji pamięci
                if (unlikely(!mem->instances[target_type] || !mem->instances_arenas[target_type] || !mem->data_pools[target_type])) {
                    EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_INSTANCE, EMU_OWNER_emu_mem_parse_create_array_instances, 0, 0, TAG, "Array: Missing allocations for Type %d Ctx %d", target_type, ctx_id);
                }

                // 1. Przygotowanie wskaźników bazowych
                void **instance_table = mem->instances[target_type];
                uint8_t *arena_base   = (uint8_t*)mem->instances_arenas[target_type];
                uint8_t *data_base    = (uint8_t*)mem->data_pools[target_type];

                // 2. Pobranie bieżących offsetów
                uint32_t instance_idx = mem->next_instance_idx[target_type];
                size_t arena_offset   = mem->instances_arenas_offset[target_type];
                size_t data_offset    = mem->next_data_pools_idx[target_type]; // To jest index elementu, nie bajtu!

                if (unlikely(instance_idx >= mem->instances_cnt[target_type])) {
                    EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_emu_mem_parse_create_array_instances, 0, 0, TAG, "Max instances reached for type %d", target_type);
                }

                // 3. Obliczenie wskaźnika na nową metadaną w arenie
                emu_mem_instance_arr_t *meta = (emu_mem_instance_arr_t*)(arena_base + arena_offset);
                
                // Zapisz wskaźnik w tabeli lookup
                instance_table[instance_idx] = meta;

                // 4. Obliczenie wskaźnika na dane
                // data_base + (index * rozmiar_typu)
                meta->data = data_base + (data_offset * DATA_TYPE_SIZES[target_type]);

                // 5. Wypełnienie pól nagłówka
                meta->dims_cnt    = dims_cnt;
                meta->target_type = target_type;
                meta->context_id  = ctx_id;
                meta->updated     = 1;
                meta->_reserved   = 0;

                // 6. Parsowanie wymiarów i obliczanie całkowitej liczby elementów
                uint32_t total_elements = 1;
                for (int d = 0; d < dims_cnt; d++) {
                    uint8_t dim_size = data[idx++];
                    meta->dim_sizes[d] = dim_size; // Flexible Array Member access
                    total_elements *= dim_size;
                }

                // 7. Aktualizacja liczników (Postęp parsowania)
                
                // Przesuwamy kursor danych o liczbę elementów (nie bajtów!)
                mem->next_data_pools_idx[target_type] += total_elements;
                
                // Przesuwamy index instancji
                mem->next_instance_idx[target_type]++;
                
                // Przesuwamy kursor areny o rozmiar struktury + rozmiar tablicy wymiarów
                // sizeof(emu_mem_instance_arr_t) uwzględnia padding, a dim_sizes[] dochodzi na końcu.
                // Drobna korekta: Flexible array member nie jest wliczony w sizeof, więc dodajemy dims_cnt.
                // Jeśli kompilator wpycha dim_sizes w padding, to i tak musimy zarezerwować bezpieczną ilość miejsca.
                // Najbezpieczniej: sizeof(struct) + dims_cnt (bajtów).
                mem->instances_arenas_offset[target_type] += sizeof(emu_mem_instance_arr_t) + (dims_cnt * sizeof(uint8_t));

                // Opcjonalne logowanie
                LOG_I(TAG, "Ctx %d: Created ARRAY Type %d (Dims:%d, Elems:%ld)", ctx_id, target_type, dims_cnt, total_elements);
            }
            EMU_RETURN_OK(EMU_LOG_arrays_created, EMU_OWNER_emu_mem_parse_create_array_instances, 0, TAG, "Arrays created for context %d", ctx_id);
        }
    }
    EMU_RETURN_WARN(EMU_ERR_PACKET_NOT_FOUND, EMU_OWNER_emu_mem_parse_create_array_instances, 0, 0, TAG, "Packet not found for VAR_H_ARR");
}



/**
 * @brief we match type to header enums
 */
static int8_t _get_type_from_header(uint16_t header, bool *is_array) {
    *is_array = false;
    switch (header) {
        case VAR_H_DATA_S_UI8:  return DATA_UI8;
        case VAR_H_DATA_S_UI16: return DATA_UI16;
        case VAR_H_DATA_S_UI32: return DATA_UI32;
        case VAR_H_DATA_S_I8:   return DATA_I8;
        case VAR_H_DATA_S_I16:  return DATA_I16;
        case VAR_H_DATA_S_I32:  return DATA_I32;
        case VAR_H_DATA_S_F:    return DATA_F;
        case VAR_H_DATA_S_D:    return DATA_D;
        case VAR_H_DATA_S_B:    return DATA_B;

        case VAR_H_DATA_ARR_UI8:  *is_array = true; return DATA_UI8;
        case VAR_H_DATA_ARR_UI16: *is_array = true; return DATA_UI16;
        case VAR_H_DATA_ARR_UI32: *is_array = true; return DATA_UI32;
        case VAR_H_DATA_ARR_I8:   *is_array = true; return DATA_I8;
        case VAR_H_DATA_ARR_I16:  *is_array = true; return DATA_I16;
        case VAR_H_DATA_ARR_I32:  *is_array = true; return DATA_I32;
        case VAR_H_DATA_ARR_F:    *is_array = true; return DATA_F;
        case VAR_H_DATA_ARR_D:    *is_array = true; return DATA_D;
        case VAR_H_DATA_ARR_B:    *is_array = true; return DATA_B;
        
        default: return -1; 
    }
}

// this is pure for parse so looks clearer
static inline emu_mem_instance_iter_t _mem_get_instance_meta(emu_mem_t *mem, uint8_t type, uint16_t idx) {
    emu_mem_instance_iter_t iter = {0};
    if (mem && idx < mem->instances_cnt[type]) {
        iter.raw = mem->instances[type][idx];
    }
    return iter;
}


static emu_result_t _parse_scalar_data(uint8_t ctx_id, uint8_t type, uint8_t *data, size_t len) {
    emu_mem_t *mem = _get_mem_context(ctx_id); 
    if (unlikely(!mem)) EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, EMU_OWNER__parse_scalar_data, 0 , 0, TAG, "Ctx %d not found", ctx_id);

    // Mimo że mamy bezpośrednie wskaźniki w instancjach, warto sprawdzić czy pula w ogóle istnieje dla bezpieczeństwa
    if (unlikely(!mem->data_pools[type])) EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER__parse_scalar_data, 0 , 0, TAG, "Data pool NULL for Ctx %d Type %d", ctx_id, type);

    size_t cursor = 0;
    size_t elem_size = DATA_TYPE_SIZES[type]; 
    size_t entry_size = 2 + elem_size; // 2 bajty indexu + wartość

    while (cursor + entry_size <= len) {
        uint16_t instance_idx = 0;
        memcpy(&instance_idx, &data[cursor], 2);
        cursor += 2;

        if (unlikely(instance_idx >= mem->instances_cnt[type])) {
            EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER__parse_scalar_data, 0 , 0, TAG, "Instance Idx OOB (%d >= %d) type %d", instance_idx, mem->instances_cnt[type], type);
        }

        // Pobranie metadanych
        emu_mem_instance_iter_t meta = _mem_get_instance_meta(mem, type, instance_idx);
        if (unlikely(!meta.raw)) {
            EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_IDX, EMU_OWNER__parse_scalar_data, 0 , 0, TAG, "Meta NULL for Inst %d Type %d", instance_idx, type);
        }

        // ZMIANA: Używamy bezpośredniego wskaźnika data zamiast start_idx
        // Wskaźnik w strukturze wskazuje już na konkretny element w puli
        uint8_t *dest = (uint8_t*)meta.single->data;
        
        // Kopiowanie danych
        memcpy(dest, &data[cursor], elem_size);
        
        cursor += elem_size;
        meta.single->updated = 1; 
    }
    EMU_RETURN_OK(EMU_LOG_data_parsed, EMU_OWNER__parse_scalar_data, 0, TAG, "Scalar data parsed for Ctx %d Type %d", ctx_id, type);
}

static emu_result_t _parse_array_data(uint8_t ctx_id, uint8_t type, uint8_t *data, size_t len) {
    emu_mem_t *mem = _get_mem_context(ctx_id);
    if (unlikely(!mem)) EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, EMU_OWNER__parse_array_data, 0 , 0, TAG, "Ctx %d not found", ctx_id);

    if (unlikely(len < 4)) {EMU_RETURN_WARN(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER__parse_array_data, 0 , 0, TAG, "Packet too small (len=%d)", (int)len);}

    uint16_t instance_idx = 0;
    memcpy(&instance_idx, &data[0], 2);
    
    uint16_t offset_in_array = 0;
    memcpy(&offset_in_array, &data[2], 2); 

    emu_mem_instance_iter_t meta = _mem_get_instance_meta(mem, type, instance_idx);
    if (unlikely(!meta.raw)) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_IDX, EMU_OWNER__parse_array_data, 0 , 0, TAG, "Meta NULL for Inst %d", instance_idx);}

    // ZMIANA: Obliczanie adresu w oparciu o wskaźnik bazowy tablicy
    // meta.arr->data wskazuje na Poczatek Tablicy (element 0)
    // Musimy przesunąć się o offset_in_array elementów
    
    size_t elem_size = DATA_TYPE_SIZES[type];
    
    // Obliczamy adres docelowy: Baza + (Offset * Rozmiar Typu)
    uint8_t *dest = (uint8_t*)meta.arr->data + (offset_in_array * elem_size); 

    // Sprawdzamy czy nie wychodzimy poza zakres pamięci (opcjonalnie, ale zalecane)
    // Do tego potrzebujesz znać całkowity rozmiar tablicy (który jest obliczany z dim_sizes)
    // Jeśli nie przechowujesz total_size w meta, to tutaj polegamy na poprawności pakietu.

    size_t data_bytes = len - 4;
    memcpy(dest, &data[4], data_bytes);
    
    meta.arr->updated = 1;
    EMU_RETURN_OK(EMU_LOG_data_parsed, EMU_OWNER__parse_array_data, 0, TAG, "Array data parsed for Ctx %d Type %d", ctx_id, type);
}
emu_result_t emu_mem_parse_context_data_packets(chr_msg_buffer_t *source, emu_mem_t *mem_ignored) {
    uint8_t *data;
    size_t len;
    size_t buff_size = chr_msg_buffer_size(source);

    for (size_t i = 0; i < buff_size; i++) {
        chr_msg_buffer_get(source, i, &data, &len);

        if (len < 3) continue;

        uint16_t header = 0;
        memcpy(&header, data, 2); 

        bool is_array = false;
        int8_t type = _get_type_from_header(header, &is_array);  //we get type from header (just map)
        emu_result_t res;

        if (type != -1) {  //-1 if unknown
            uint8_t ctx_id = data[2];  //ctx is common for whole packet
            uint8_t *payload = &data[3];
            size_t payload_len = len - 3;

            if (is_array) {
                res = _parse_array_data(ctx_id, type, payload, payload_len); //invoke right function
            } else {    
                res = _parse_scalar_data(ctx_id, type, payload, payload_len);
            }
            //check for errors form parsers
            if (res.abort) {
                EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_mem_parse_context_data_packets, 0, ++res.depth, TAG, "Data Parse: Pkt %d Fail (Ctx:%d Type:%s Arr:%d) ", (int)i, ctx_id, EMU_DATATYPE_TO_STR[type], is_array);
            } else if (res.warning){
                //report warning but don't return
                EMU_REPORT(res.code, EMU_OWNER_emu_mem_parse_context_data_packets, 0, TAG, "Data Parse: Pkt %d Warn (Ctx:%d Type:%s Arr:%d) ", (int)i, ctx_id, EMU_DATATYPE_TO_STR[type], is_array);
            } else {
                LOG_I(TAG, "Data Parse: Pkt %d OK (Ctx:%d Type:%s Arr:%d)", (int)i, ctx_id, EMU_DATATYPE_TO_STR[type], is_array);
            }
        }
    }
    EMU_RETURN_OK(EMU_LOG_data_parsed, EMU_OWNER_emu_mem_parse_context_data_packets, 0, TAG, "Data packets parsed successfully");
}
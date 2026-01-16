#include "emulator_variables_acces.h"
#include "emulator_blocks.h"
#include "emulator_logging.h"
#include "mem_types.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG_VARS = "EMU_VARS";
static const char *TAG_REF  = "EMU_REF";
static const char *TAG      = "EMU_MEM";

extern emu_mem_t* s_mem_contexts[DATA_TYPES_CNT];


/**************************************************************************************************
*
* MEMORY POOL (CONTEXTS)
*
****************************************************************************************************/


/** 
* @brief stores structs for scalar access
*/
typedef struct {
    uint8_t *buffer;
    size_t   item_size;
    size_t   capacity;
    size_t   used_count;
    const char *tag;
} mem_pool_acces_s_t;


/** 
* @brief stores struct for array access
*/
typedef struct {
    uint8_t *buffer;
    size_t   total_size;
    size_t   used_bytes;
    const char *tag;
} mem_pool_acces_arr_t;


//Internal - create pool for scalars access
static emu_result_t mem_pool_access_scalar_create(mem_pool_acces_s_t *pool, size_t item_size, size_t capacity, const char *tag) {
    pool->item_size = item_size;
    pool->capacity = capacity;
    pool->used_count = 0;
    pool->tag = tag;
    pool->buffer = (uint8_t*)malloc(item_size * capacity);
    //just simple verification
    if (!pool->buffer) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_ALLOC, EMU_OWNER_mem_pool_access_scalar_create, 0, 0, TAG, "Scalar pool alloc failed for %s", tag);}
    memset(pool->buffer, 0, item_size * capacity);
    EMU_RETURN_OK(EMU_LOG_access_pool_allocated, EMU_OWNER_mem_pool_access_scalar_create, 0, TAG, "Scalar pool created for %s (Item Size:%d, Capacity:%d)", tag, (int)item_size, (int)capacity);
}   
//just helper we return ptr (next one)
static void* mem_pool_acces_scalar_alloc(mem_pool_acces_s_t *pool) {
    if (pool->used_count >= pool->capacity) return NULL;
    void *ptr = pool->buffer + (pool->used_count * pool->item_size);
    pool->used_count++;
    return ptr;
}

//delete whole pool
static void mem_pool_acces_scalar_destroy(mem_pool_acces_s_t *pool) {
    if (pool->buffer) free(pool->buffer);
    pool->buffer = NULL;
    pool->used_count = 0;
}

//Create pool of given size for arrays
static emu_result_t mem_pool_acces_arr_create(mem_pool_acces_arr_t *pool, size_t size, const char *tag) {
    pool->total_size = size;
    pool->used_bytes = 0;
    pool->tag = tag;
    pool->buffer = (uint8_t*)malloc(size);
    if (!pool->buffer) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_ALLOC, EMU_OWNER_mem_pool_access_scalar_create,0, 0, TAG, "Array pool alloc failed for %s", tag);}
    memset(pool->buffer, 0, size);
    EMU_RETURN_OK(EMU_LOG_access_pool_allocated, EMU_OWNER_mem_pool_access_scalar_create, 0, TAG, "Array pool created for %s (Size:%d B)", tag, (int)size);
}

//Internal for scalars (gives pointer to start of space of chosen size)
static void* mem_pool_access_arr_alloc(mem_pool_acces_arr_t *pool, size_t size) {
    size_t aligned_size = (size + 3) & ~3;
    if (pool->used_bytes + aligned_size > pool->total_size) return NULL;
    void *ptr = pool->buffer + pool->used_bytes;
    pool->used_bytes += aligned_size;
    return ptr;
}

//delete pool for arrays
static void mem_pool_acces_arr_destroy(mem_pool_acces_arr_t *pool) {
    if (pool->buffer) free(pool->buffer);
    pool->buffer = NULL;
    pool->used_bytes = 0;
}


//This function is used within mem_get to resolve array element index by recursive access
static __always_inline emu_err_t _resolve_mem_offset(void *access_node, uint32_t *out_offset) {
    mem_access_arr_t *node = (mem_access_arr_t*)access_node;
    emu_mem_instance_arr_t *instance = (emu_mem_instance_arr_t*)node->instance;

    uint8_t dims_cnt = instance->dims_cnt;

    if (unlikely(dims_cnt == 0)) {
        *out_offset = 0;
        return EMU_OK;
    }

    uint32_t final_index = 0;
    uint32_t stride = 1;

    // Iteracja od ostatniego wymiaru (Row-major order)
    for (int8_t i = (int8_t)dims_cnt - 1; i >= 0; i--) {
        uint32_t index_val = 0;

        if (IDX_IS_DYNAMIC(node, i)) { 
            emu_variable_t idx_var = mem_get(node->indices[i].next_instance, false);
            if (unlikely(idx_var.error)) { return idx_var.error; }
            index_val = MEM_CAST(idx_var, (uint32_t)0);
        } else {
            index_val = node->indices[i].static_idx;
        }

        // Sprawdzenie granic (Bounds Check)
        if (unlikely(index_val >= instance->dim_sizes[i])) {
            EMU_REPORT(EMU_LOG_access_out_of_bounds, EMU_OWNER__resolve_mem_offset, 0, TAG, 
                       "Array OOB Dim %d: %ld >= %d", i, index_val, instance->dim_sizes[i]);
            return EMU_ERR_MEM_OUT_OF_BOUNDS; 
        }

        final_index += index_val * stride;
        stride *= instance->dim_sizes[i];
    }

    *out_offset = final_index;
    return EMU_OK;
}
//This function must be as fast as possible as we use it very often, for nearly every operation
emu_variable_t mem_get(void *mem_access_x, bool by_reference) {
    emu_variable_t var = {0};
    
    mem_access_s_t *target = (mem_access_s_t*)mem_access_x;
    if (unlikely(!target)) { var.error = EMU_ERR_NULL_PTR; goto error_early; }

    emu_mem_instance_s_t *instance = (emu_mem_instance_s_t*)target->instance;
    if (unlikely(!instance)) { var.error = EMU_ERR_NULL_PTR; goto error_early; }

    // Pobieramy typ z instancji (jest pewniejszy)
    uint8_t type = instance->target_type;
    var.type = type;

    // --- ŚCIEŻKA SZYBKA: Resolved (używamy Unii w target) ---
    if (likely(target->is_resolved)) {
        if (by_reference) {
            var.by_reference = 1;
            switch (type) {
                case DATA_UI8:  var.reference.u8  = target->direct_ptr.static_ui8; break;
                case DATA_UI16: var.reference.u16 = target->direct_ptr.static_ui16; break;
                case DATA_UI32: var.reference.u32 = target->direct_ptr.static_ui32; break;
                case DATA_I8:   var.reference.i8  = target->direct_ptr.static_i8; break;
                case DATA_I16:  var.reference.i16 = target->direct_ptr.static_i16; break;
                case DATA_I32:  var.reference.i32 = target->direct_ptr.static_i32; break;
                case DATA_F:    var.reference.f   = target->direct_ptr.static_f; break;
                case DATA_D:    var.reference.d   = target->direct_ptr.static_d; break;
                case DATA_B:    var.reference.b   = target->direct_ptr.static_b; break;
                default: var.error = EMU_ERR_MEM_INVALID_DATATYPE; goto error;
            }
        } else {
            var.by_reference = 0;
            switch (type) {
                case DATA_UI8:  var.data.u8  = *target->direct_ptr.static_ui8; break;
                case DATA_UI16: var.data.u16 = *target->direct_ptr.static_ui16; break;
                case DATA_UI32: var.data.u32 = *target->direct_ptr.static_ui32; break;
                case DATA_I8:   var.data.i8  = *target->direct_ptr.static_i8; break;
                case DATA_I16:  var.data.i16 = *target->direct_ptr.static_i16; break;
                case DATA_I32:  var.data.i32 = *target->direct_ptr.static_i32; break;
                case DATA_F:    var.data.f   = *target->direct_ptr.static_f; break;
                case DATA_D:    var.data.d   = *target->direct_ptr.static_d; break;
                case DATA_B:    var.data.b   = *target->direct_ptr.static_b; break;
                default: var.error = EMU_ERR_MEM_INVALID_DATATYPE; goto error;
            }
        }
    } 
    // --- ŚCIEŻKA WOLNA: Dynamiczna (obliczanie offsetu) ---
    else {
        uint32_t elem_offset = 0;
        
        var.error = _resolve_mem_offset(mem_access_x, &elem_offset);
        if (unlikely(var.error != EMU_OK)) goto error;

        // ZMIANA: Bierzemy wskaźnik bazowy z instancji!
        void *base_ptr = instance->data;
        if (unlikely(!base_ptr)) { var.error = EMU_ERR_NULL_PTR; goto error; }

        if (by_reference) {
            var.by_reference = 1;
            switch (type) {
                // Rzutowanie (typ*)base_ptr przesuwa wskaźnik o (elem_offset * sizeof(typ))
                case DATA_UI8:  var.reference.u8  = &((uint8_t*)base_ptr)[elem_offset]; break;
                case DATA_UI16: var.reference.u16 = &((uint16_t*)base_ptr)[elem_offset]; break;
                case DATA_UI32: var.reference.u32 = &((uint32_t*)base_ptr)[elem_offset]; break;
                case DATA_I8:   var.reference.i8  = &((int8_t*)base_ptr)[elem_offset]; break;
                case DATA_I16:  var.reference.i16 = &((int16_t*)base_ptr)[elem_offset]; break;
                case DATA_I32:  var.reference.i32 = &((int32_t*)base_ptr)[elem_offset]; break;
                case DATA_F:    var.reference.f   = &((float*)base_ptr)[elem_offset]; break;
                case DATA_D:    var.reference.d   = &((double*)base_ptr)[elem_offset]; break;
                case DATA_B:    var.reference.b   = &((bool*)base_ptr)[elem_offset]; break;
                default: var.error = EMU_ERR_MEM_INVALID_DATATYPE; goto error;
            }
        } else {
            var.by_reference = 0;
            switch (type) {
                case DATA_UI8:  var.data.u8  = ((uint8_t*)base_ptr)[elem_offset]; break;
                case DATA_UI16: var.data.u16 = ((uint16_t*)base_ptr)[elem_offset]; break;
                case DATA_UI32: var.data.u32 = ((uint32_t*)base_ptr)[elem_offset]; break;
                case DATA_I8:   var.data.i8  = ((int8_t*)base_ptr)[elem_offset]; break;
                case DATA_I16:  var.data.i16 = ((int16_t*)base_ptr)[elem_offset]; break;
                case DATA_I32:  var.data.i32 = ((int32_t*)base_ptr)[elem_offset]; break;
                case DATA_F:    var.data.f   = ((float*)base_ptr)[elem_offset]; break;
                case DATA_D:    var.data.d   = ((double*)base_ptr)[elem_offset]; break;
                case DATA_B:    var.data.b   = ((bool*)base_ptr)[elem_offset]; break;
                default: var.error = EMU_ERR_MEM_INVALID_DATATYPE; goto error;
            }
        }
    }
    
    return var;

error:
    EMU_REPORT(EMU_LOG_mem_get_failed, EMU_OWNER_mem_get, 0, TAG_VARS, 
               "Failed:%s, Type:%d, OffsetCheck", EMU_ERR_TO_STR(var.error), type);
    return var;

error_early:
    return var;
}


emu_result_t mem_set(void *mem_access_x, emu_variable_t val) {
    mem_access_s_t *dst = (mem_access_s_t*)mem_access_x;
    
    if (unlikely(!dst)) {
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR, EMU_OWNER_mem_set, 0, 0, TAG, "Access node is NULL");
    }

    // 1. Pobierz wskaźnik do pamięci docelowej (By Reference = true)
    // To wywołanie załatwia za nas całą matematykę offsetów (dla tablic) i typów.
    emu_variable_t dst_ptr = mem_get(mem_access_x, true);
    
    if (unlikely(dst_ptr.error != EMU_OK)) {
        EMU_RETURN_CRITICAL(dst_ptr.error, EMU_OWNER_mem_set, 0, 1, TAG, "Dst resolve error: %s", EMU_ERR_TO_STR(dst_ptr.error));
    }

    emu_mem_instance_s_t *meta = (emu_mem_instance_s_t*)dst->instance;
    
    if (likely(meta)) {
        meta->updated = 1;
    } else {
        // To rzadki błąd krytyczny - mamy dostęp, ale brak instancji?
        EMU_RETURN_CRITICAL(EMU_ERR_NULL_PTR_INSTANCE, EMU_OWNER_mem_set, 0, 0, TAG, "Instance meta is NULL");
    }

    // 3. Konwersja i rzutowanie wartości (bez zmian, logika jest dobra)
    double src_val = MEM_CAST(val, (double)0);
    double rnd = (dst_ptr.type < DATA_F) ? round(src_val) : src_val;

    // 4. Zapis wartości pod uzyskany adres
    switch (dst_ptr.type) {
        case DATA_UI8:  *dst_ptr.reference.u8  = CLAMP_CAST(rnd, 0, UINT8_MAX, uint8_t); break;
        case DATA_UI16: *dst_ptr.reference.u16 = CLAMP_CAST(rnd, 0, UINT16_MAX, uint16_t); break;
        case DATA_UI32: *dst_ptr.reference.u32 = CLAMP_CAST(rnd, 0, UINT32_MAX, uint32_t); break;
        case DATA_I8:   *dst_ptr.reference.i8  = CLAMP_CAST(rnd, INT8_MIN, INT8_MAX, int8_t); break;
        case DATA_I16:  *dst_ptr.reference.i16 = CLAMP_CAST(rnd, INT16_MIN, INT16_MAX, int16_t); break;
        case DATA_I32:  *dst_ptr.reference.i32 = CLAMP_CAST(rnd, INT32_MIN, INT32_MAX, int32_t); break;
        case DATA_F:    *dst_ptr.reference.f   = (float)src_val; break;
        case DATA_D:    *dst_ptr.reference.d   = src_val; break;
        case DATA_B:    *dst_ptr.reference.b   = (src_val != 0.0); break;
        
        default: 
            EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_DATATYPE, EMU_OWNER_mem_set, 0, 0, TAG, "Invalid Dest Type %d", dst_ptr.type);
    }
    return EMU_RESULT_OK();
}


/*****************************************************************************
 * 
 * References allocation (similar to instances one)
 * 
 ***************************************************************************/

 //from header
static mem_pool_acces_s_t s_scalar_pool;
static mem_pool_acces_arr_t  s_arena_pool;

static bool s_is_initialized = false;


emu_result_t emu_access_system_init(size_t max_scalars, size_t arrays_arena_bytes) {
    if (s_is_initialized) emu_access_system_free();
    emu_result_t res;
    //we create scalar pool of given count 
    res = mem_pool_access_scalar_create(&s_scalar_pool, sizeof(mem_access_s_t), max_scalars, "REF_SCAL");
    if(res.abort){
        mem_pool_acces_arr_destroy(&s_arena_pool);
        EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_access_system_init, 0, ++res.depth, TAG_REF, "Scalar Pool Fail");}
    if(res.warning){
        s_is_initialized = true;
        EMU_RETURN_WARN(res.code, EMU_OWNER_emu_access_system_init, 0, ++res.depth, TAG_REF, "Scalar Pool Warn");}
    
    res= mem_pool_acces_arr_create(&s_arena_pool, arrays_arena_bytes, "REF_ARR");
    if(res.abort){ 
        mem_pool_acces_scalar_destroy(&s_scalar_pool);
        EMU_RETURN_CRITICAL(res.code, EMU_OWNER_emu_access_system_init, 0, ++res.depth, TAG_REF, "Arena Pool Fail");
    }
    if(res.warning){
        s_is_initialized = true;
        EMU_RETURN_WARN(res.code, EMU_OWNER_emu_access_system_init, 0, ++res.depth, TAG_REF, "Arena Pool Warn");}
    s_is_initialized = true;
    EMU_RETURN_OK(EMU_LOG_access_pool_allocated, EMU_OWNER_emu_access_system_init, 0, TAG_REF, "Init: Scalars=%d, Arena=%d bytes", (int)max_scalars, (int)arrays_arena_bytes);
}

void emu_access_system_free(void) {
    if (s_is_initialized) {
        mem_pool_acces_scalar_destroy(&s_scalar_pool);
        mem_pool_acces_arr_destroy(&s_arena_pool);
        s_is_initialized = false;
        EMU_REPORT(EMU_LOG_access_pool_allocated, EMU_OWNER_emu_access_system_free, 0, TAG_REF, "Access System Freed");
    }
}



emu_result_t mem_access_parse_node_recursive(uint8_t **cursor, size_t *len, void **out_node) {
    // 1. Weryfikacja nagłówka (3 bajty)
    if (*len < 3) {
        EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Incomplete main node header");
    }

    uint8_t h = (*cursor)[0];
    uint8_t dims = h & 0x0F;          // 4 bity: liczba wymiarów
    uint8_t type = (h >> 4) & 0x0F;   // 4 bity: typ danych
    
    uint16_t idx; 
    memcpy(&idx, &(*cursor)[1], 2);   // 2 bajty: indeks instancji
    *cursor += 3; *len -= 3;

    // --- CASE 1: SKALAR (dims == 0) ---
    if (dims == 0) {
        if (*len < 1) {
            EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Scalar: Incomplete ref byte");
        }
        uint8_t ref_byte = (*cursor)[0];
        *cursor += 1; *len -= 1;

        // Alokacja węzła dostępu
        mem_access_s_t *n = (mem_access_s_t*)mem_pool_acces_scalar_alloc(&s_scalar_pool);
        if (!n) {EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Ref Arena Exhausted!");}

        uint8_t ref_id = ref_byte & 0x0F;
        emu_mem_t *s_mem = s_mem_contexts[ref_id];
        if (!s_mem) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Invalid Ref ID %d", ref_id);}

        // Pobranie instancji
        if (unlikely(idx >= s_mem->instances_cnt[type])) {
             EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Idx OOB %d", idx);
        }
        emu_mem_instance_iter_t meta;
        meta.raw = (uint8_t*)s_mem->instances[type][idx]; // Wskaźnik void* w tabeli

        if (!meta.raw) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Instance NULL Type %d Idx %d", type, idx);}
        
        // Wypełnianie węzła dostępu
        n->instance = meta.raw; // KLUCZOWE: Zapisujemy wskaźnik do instancji
        n->is_resolved = 1;     // Skalar jest zawsze resolved

        // ZAMIANA: Używamy wskaźnika data z instancji jako bazy
        void *data_ptr = meta.single->data;

        // Przypisanie do Unii (Zachowujemy Switch dla poprawności typów w Unii)
        switch (type) {
            case DATA_UI8:  n->direct_ptr.static_ui8  = (uint8_t*)data_ptr; break;
            case DATA_UI16: n->direct_ptr.static_ui16 = (uint16_t*)data_ptr; break;
            case DATA_UI32: n->direct_ptr.static_ui32 = (uint32_t*)data_ptr; break;
            case DATA_I8:   n->direct_ptr.static_i8   = (int8_t*)data_ptr; break;
            case DATA_I16:  n->direct_ptr.static_i16  = (int16_t*)data_ptr; break;
            case DATA_I32:  n->direct_ptr.static_i32  = (int32_t*)data_ptr; break;
            case DATA_F:    n->direct_ptr.static_f    = (float*)data_ptr; break;
            case DATA_D:    n->direct_ptr.static_d    = (double*)data_ptr; break;
            case DATA_B:    n->direct_ptr.static_b    = (bool*)data_ptr; break;
            default: 
                EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_DATATYPE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Invalid Data Type %d", type);
        }

        *out_node = n;
        EMU_RETURN_OK(EMU_LOG_mem_access_parse_success, EMU_OWNER_mem_access_parse_node_recursive, 0, TAG_REF, "Parsed Scalar Ref: ctx %d, type %d", ref_id, type);
    } 
    
    // --- CASE 2: TABLICA (dims > 0) ---
    else {
        if (*len < 1) {EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Array: Incomplete ref byte");}
        uint8_t config_byte = (*cursor)[0];
        *cursor += 1; *len -= 1;

        size_t size = sizeof(mem_access_arr_t) + (dims * sizeof(idx_u));
        mem_access_arr_t *n = (mem_access_arr_t*)mem_pool_access_arr_alloc(&s_arena_pool, size);
        if (!n) {EMU_RETURN_CRITICAL(EMU_ERR_NO_MEM, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Ref Arena Exhausted!");}
        
        uint8_t ref_id = config_byte & 0x0F;
        emu_mem_t *s_mem = s_mem_contexts[ref_id];
        if (!s_mem) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_INVALID_REF_ID, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Invalid Ref ID %d", ref_id);}
        
        emu_mem_instance_iter_t meta;
        meta.raw = (uint8_t*)s_mem->instances[type][idx];
        if (!meta.raw) {EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Out of Bounds Instance for Type %d Idx %d", type, idx);}

        // Wypełnianie węzła
        n->instance = meta.raw; // KLUCZOWE: Wskaźnik do metadanych
        n->is_idx_static = (config_byte >> 4) & 0x0F; // Maska bitowa statyczności

        // Parsowanie indeksów
        bool is_dynamic = false; 
        for (int i = 0; i < dims; i++) {
            if (IDX_IS_DYNAMIC(n, i)) { // Sprawdzamy bit w masce
                is_dynamic = true;
                emu_result_t res = mem_access_parse_node_recursive(cursor, len, &n->indices[i].next_instance);
                if (res.abort) {
                    EMU_RETURN_CRITICAL(res.code, EMU_OWNER_mem_access_parse_node_recursive, 0, ++res.depth, TAG_REF, "Array: Failed to parse dynamic index (Dim %d)", i);
                }
            } else { 
                if (*len < 2) {
                    EMU_RETURN_CRITICAL(EMU_ERR_PACKET_INCOMPLETE, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Array: Incomplete static index data (Dim %d)", i);
                }
                memcpy(&n->indices[i].static_idx, *cursor, 2); 
                *cursor += 2; *len -= 2; 
            }
        }

        // Obliczanie adresu dla tablic w pełni statycznych
        if (!is_dynamic) {       
            // Obliczamy offset (numer elementu)
            // Używamy zaktualizowanej funkcji resolve_mem_offset (lub logiki tutaj)
            // Ponieważ tutaj mamy dostęp do 'n', a funkcja resolve działa na 'n', możemy jej użyć!
            // Ale uwaga: resolve zwraca offset elementu, nie bajtów.
            
            // Ręczne obliczenie tutaj dla pewności (skopiowane z poprzedniej logiki):
            uint32_t final_element_offset = 0;
            uint32_t stride = 1;

            // Iteracja od tyłu (zgodnie z resolve_mem_offset logic)
            for (int8_t i = (int8_t)dims - 1; i >= 0; i--) {
                uint16_t s_idx = n->indices[i].static_idx;
                if (s_idx >= meta.arr->dim_sizes[i]) {
                     EMU_RETURN_CRITICAL(EMU_ERR_MEM_OUT_OF_BOUNDS, EMU_OWNER_mem_access_parse_node_recursive, 0, 0, TAG_REF, "Static Index OOB");
                }
                final_element_offset += s_idx * stride;
                stride *= meta.arr->dim_sizes[i];
            }

            // Mamy finalny offset. Baza to meta.arr->data.
            n->is_resolved = 1;
            
            // Obliczamy fizyczny adres w pamięci
            // Musimy wiedzieć jaki jest rozmiar typu elementu
            // Możemy użyć Twojej tablicy DATA_TYPE_SIZES (jeśli jest dostępna)
            // Lub switcha jak wyżej.
            
            // ZAMIANA: meta.arr->data to start tablicy.
            // Pointer arithmetic na typowanym wskaźniku załatwi sprawę.
            
            switch (type) {
                case DATA_UI8:  n->direct_ptr.static_ui8  = ((uint8_t*)meta.arr->data)  + final_element_offset; break;
                case DATA_UI16: n->direct_ptr.static_ui16 = ((uint16_t*)meta.arr->data) + final_element_offset; break;
                // ... (reszta typów)
                case DATA_F:    n->direct_ptr.static_f    = ((float*)meta.arr->data)    + final_element_offset; break;
                // ...
                default: 
                     // Obsłuż błąd
                     break;
            }

        } else {            
            n->is_resolved = 0;
            // direct_ptr zostaje puste/śmieciowe, bo użyjemy instance->data w mem_get
        }
        
        *out_node = n; 
        EMU_RETURN_OK(EMU_LOG_mem_access_parse_success, EMU_OWNER_mem_access_parse_node_recursive, 0, TAG_REF, "Parsed Array Ref: ctx %d, type %d", ref_id, type);
    }
}
#include "emulator_blocks.h"
#include "string.h"
#include "emulator_variables.h"
#include "emulator_block_utils.h"
#include <math.h>
#include <float.h>
#include <stdbool.h>

static void block_pass_results(block_handle_t* block);
extern block_handle_t** blocks_structs;
expression_t *expression_table[5];

static inline bool is_greater(double a, double b);
static inline bool is_equal(double a, double b);
static inline bool is_zero(double a);

static int cnt = 0;
emu_err_t block_compute(block_handle_t* block){
    
    expression_t* eval = (expression_t*)block->extras;
    double stack[16];
    int over_top = 0;
    double result = 0;

    for(int i = 0; i<eval->count; i++){
        instruction_t *ins = &(eval->code[i]);
        switch(ins->op){
            case OP_VAR:
                stack[over_top++] = get_in_val(ins->input_index, block);
                break;

            case OP_CONST:
                stack[over_top++] = eval->constant_table[ins->input_index];
                break;

            case OP_ADD:
                stack[over_top-2] = stack[over_top-2] + stack[over_top-1];
                over_top--;
                break;

            case OP_MUL:
                stack[over_top-2] = stack[over_top-2] * stack[over_top-1];
                over_top--;
                break;

            case OP_DIV:
                if(is_zero(stack[over_top-1]))
                    return EMU_ERR_DIVISION_BY_ZERO;

                stack[over_top-2] = stack[over_top-2]/stack[over_top-1];
                over_top--;
                break;

            case OP_COS:
                stack[over_top-1] = cos(stack[over_top-1]);
                break;

            case OP_SIN:
                stack[over_top-1] = sin(stack[over_top-1]);
                break; 

            case OP_POWER:
                stack[over_top-2] = pow(stack[over_top-2], stack[over_top-1]);
                over_top--;
                break;

            case OP_ROOT:
                stack[over_top-1] = sqrt(stack[over_top-1]);
                break;

            case OP_SUB:
                stack[over_top-2] = stack[over_top-2] - stack[over_top-1];
                over_top--;
                break; 
        }
    }

    result = stack[0];
    q_set_value(block, 0, result);
    block_pass_results(block);
    return EMU_OK;
}

emu_err_t example_block(block_handle_t *block){
    return EMU_OK;
    block_pass_results(block);
}

static void block_pass_results(block_handle_t* block)
{
    if (!block || !block->q_connections_table) return;

    for (uint8_t q_idx = 0; q_idx < block->q_cnt; q_idx++) {
        void* src = (uint8_t*)block->q_data + block->q_data_offsets[q_idx];
        data_types_t src_type = block->q_data_type_table[q_idx];

        for (uint8_t conn_idx = 0; conn_idx < block->q_connections_table[q_idx].conn_cnt; conn_idx++) {
            block_handle_t* target_block = blocks_structs[block->q_connections_table[q_idx].target_blocks_id_list[conn_idx]];
            if (!target_block) continue;

            uint8_t target_input = block->q_connections_table[q_idx].target_inputs_list[conn_idx];
            void* dst = (uint8_t*)target_block->in_data + target_block->in_data_offsets[target_input];
            data_types_t dst_type = target_block->in_data_type_table[target_input];

            double val = 0.0;
            switch (src_type) {
                case DATA_UI8:  { uint8_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_UI16: { uint16_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_UI32: { uint32_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_I8:   { int8_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_I16:  { int16_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_I32:  { int32_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_F:    { float tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp; } break;
                case DATA_D:    { memcpy(&val, src, sizeof(double)); } break;
                case DATA_B:    { uint8_t tmp; memcpy(&tmp, src, sizeof(tmp)); val = tmp != 0; } break;
                default: ESP_LOGE("COPY_DBG","unknown src type %d", src_type); continue;
            }

            if (!(dst_type == DATA_F || dst_type == DATA_D)) val = round(val);

            switch (dst_type) {
                case DATA_UI8:  { if (val < 0) val = 0; if (val > UINT8_MAX) val = UINT8_MAX; *(uint8_t*)dst = (uint8_t)val; } break;
                case DATA_UI16: { if (val < 0) val = 0; if (val > UINT16_MAX) val = UINT16_MAX; *(uint16_t*)dst = (uint16_t)val; } break;
                case DATA_UI32: { if (val < 0) val = 0; if (val > UINT32_MAX) val = UINT32_MAX; *(uint32_t*)dst = (uint32_t)val; } break;
                case DATA_I8:   { if (val < INT8_MIN) val = INT8_MIN; if (val > INT8_MAX) val = INT8_MAX; *(int8_t*)dst = (int8_t)val; } break;
                case DATA_I16:  { if (val < INT16_MIN) val = INT16_MIN; if (val > INT16_MAX) val = INT16_MAX; *(int16_t*)dst = (int16_t)val; } break;
                case DATA_I32:  { if (val < INT32_MIN) val = INT32_MIN; if (val > INT32_MAX) val = INT32_MAX; *(int32_t*)dst = (int32_t)val; } break;
                case DATA_F:    { *(float*)dst = (float)val; } break;
                case DATA_D:    { memcpy(dst, &val, sizeof(double)); } break;
                case DATA_B:    { *(uint8_t*)dst = (val != 0.0) ? 1 : 0; } break;
                default: ESP_LOGE("COPY_DBG","unknown dst type %d", dst_type); break;
            }
        }
    }
}


void free_block(block_handle_t* block) {
    if (!block) return;

    // Free input-related memory
    if (block->in_data_type_table) free(block->in_data_type_table);
    if (block->in_data_offsets) free(block->in_data_offsets);
    if (block->in_data) free(block->in_data);

    // Free output-related memory
    if (block->q_data_type_table) free(block->q_data_type_table);
    if (block->q_data_offsets) free(block->q_data_offsets);
    if (block->q_data) free(block->q_data);

    // Free connections table
    if (block->q_connections_table) {
        for (uint8_t q = 0; q < block->q_cnt; q++) {
            q_connection_t* conn = &block->q_connections_table[q];
            if (conn->target_blocks_id_list) free(conn->target_blocks_id_list);
            if (conn->target_inputs_list) free(conn->target_inputs_list);
        }
        free(block->q_connections_table);
    }

    free(block);
}

void blocks_free_all(block_handle_t** blocks_structs, uint16_t num_blocks) {
    if (!blocks_structs) return;

    for (size_t i = 0; i < num_blocks; i++) {
        free_block(blocks_structs[i]);
    }
    free(blocks_structs);
}

static inline bool is_equal(double a, double b){
    return fabs(a-b)<DBL_EPSILON;
}

static inline bool is_zero(double a){
    return fabs(a)<DBL_EPSILON;
}

static inline bool is_greater(double a, double b){
    return fabs(a-b)>DBL_EPSILON;
}


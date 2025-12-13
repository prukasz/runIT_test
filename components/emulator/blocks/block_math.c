#include "block_math.h"
#include "emulator_errors.h"
#include <math.h>
#include <float.h>
#include <stdbool.h>

int cnt;
emu_err_t block_math(block_handle_t* block_data){
    double val ;
    // uint8_t table[3]= {0xFF,0xFF,0xFF};
    // //ESP_LOGI("GLOBALACCES","resuld by hand %d",MEM_GET_U8(0,table));

    // uint8_t table2[3]= {0x01,0xFF,0xFF};
    // //ESP_LOGI("GLOBALACCES","res uld by hand2 %d",MEM_GET_U16(0,table2));
    double result = 0.0;
    uint8_t table[3]= {0,0,255};
    ESP_LOGI("normall access","result1 %lf",MEM_GET_F(0, table));
    utils_global_var_acces_recursive(block_data->global_reference[0], &result);
    ESP_LOGI("GLOBALACCES","result1 %lf", result);
    utils_global_var_acces_recursive(block_data->global_reference[1], &result);
    ESP_LOGI("GLOBALACCES","result2 %lf", result);
        // utils_global_var_acces_recursive(test, &result);
        // utils_global_var_acces_recursive(test, &result);
        // utils_global_var_acces_recursive(test, &result);
       
    expression_t* eval = (expression_t*)block->extras;
    double stack[16];
    int over_top = 0;
    double result = 0;

    for(int i = 0; i<eval->count; i++){
        instruction_t *ins = &(eval->code[i]);
        switch(ins->op){
            case OP_VAR:
                utils_get_in_val_autoselect(ins->input_index, block, &stack[over_top++]);
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
                    return EMU_ERR_DIV_BY_ZERO;

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
    LOG_I("BLOCK_MATH", "Computed result: %lf", result);
    utils_set_q_val(block, 0, (void*)&result);
    block_pass_results(block);
    return EMU_OK;      
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

emu_err_t _clear_expression_internals(expression_t* expr){
    if(expr->code){
        free(expr->code);
    }
    if(expr->constant_table){
        free(expr->constant_table);
    }
    LOG_I("MATH_CLEANUP", "Cleared expression memory");
    return EMU_OK;
}

emu_err_t emu_math_block_free_expression(block_handle_t* block){
    expression_t* expr = (expression_t*)block->extras;
    if(expr){
        _clear_expression_internals(expr);
        free(expr);
        LOG_I("MATH_CLEANUP", "Cleared block extras memory");
    }
    return EMU_OK;
}

#include "block_set_global.h"
#include "emulator_variables_acces.h" 
#include "esp_log.h"

extern emu_mem_t *mem;

static const char* TAG = "BLOCK_SET_GLOBAL";

emu_result_t block_set_global(block_handle_t* block_data) {
    if (!block_data || !block_data->global_references_list || block_data->cfg.global_reference_cnt == 0) {
        return EMU_RESULT_CRITICAL(EMU_ERR_NULL_PTR, block_data->cfg.block_idx);
    }

    // 1. Pobierz wartość do ustawienia z wejścia 0
    emu_variable_t val_to_set = utils_get_in_val_auto(block_data, 0);

    // 2. Wybierz oryginalny root ze struktury
    void* original_root = block_data->global_references_list[0];
    if (block_data->cfg.global_reference_cnt >= 2) {
        original_root = block_data->global_references_list[1];
    }

    // 3. Pobierz nagłówek, żeby sprawdzić wymiary
    mem_acces_s_t* header = (mem_acces_s_t*)original_root;
    
    // Jeśli to skalar (0 dims), po prostu przesyłamy oryginalny node
    if (header->dims_cnt == 0) {
        emu_err_t err = mem_set(mem, original_root, val_to_set);
        if (err != EMU_OK) return EMU_RESULT_CRITICAL(err, block_data->cfg.block_idx);
    } 
    else {
        // --- LOGIKA NADPISYWANIA INDEKSÓW (Dla Tablic) ---
        
        // Tworzymy kopię struktury na stosie (Temporary Fake Node)
        // Używamy unii lub bufora, żeby zmieścić max wymiary (mem_acces_arr_t + 3 * idx_u)
        uint8_t fake_node_buf[sizeof(mem_acces_arr_t) + (3 * sizeof(idx_u))];
        mem_acces_arr_t* fake_node = (mem_acces_arr_t*)fake_node_buf;
        mem_acces_arr_t* arr_root = (mem_acces_arr_t*)original_root;

        // Kopiujemy bazowe informacje (type, idx, dims_cnt)
        memcpy(fake_node, arr_root, sizeof(mem_acces_arr_t));
        
        // Ważne: w fake_node wyłączamy bity dynamiczne dla nadpisanych wymiarów
        for (uint8_t i = 0; i < header->dims_cnt; i++) {
            uint8_t in_idx = i + 1; // Wejścia bloku 1, 2, 3

            if (block_data->cfg.in_used & (1 << in_idx)) {
                // Nadpisujemy: ustawiamy jako STATIC i wpisujemy wartość z wejścia bloku
                emu_variable_t v_idx = utils_get_in_val_auto(block_data, in_idx);
                fake_node->idx_types &= ~(1 << i); // Bit na 0 (Static)
                fake_node->indices[i].static_idx = (uint16_t)emu_var_to_double(v_idx);
            } else {
                // Zachowujemy oryginał (kopiujemy wskaźnik next_node lub wartość static)
                fake_node->indices[i] = arr_root->indices[i];
                // Bit idx_types jest już skopiowany przez memcpy
            }
        }

        // Wywołujemy Twoje mem_set podając nasz "podrobiony" węzeł ze stosu
        emu_err_t err = mem_set(mem, fake_node, val_to_set);
        if (err != EMU_OK) return EMU_RESULT_CRITICAL(err, block_data->cfg.block_idx);
    }

    block_pass_results(block_data);
    return EMU_RESULT_OK();
}
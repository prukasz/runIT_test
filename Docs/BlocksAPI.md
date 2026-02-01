Blocks API

This document describes the minimal API a block implementation must provide and the helper functions provided by the emulator core.

## Required block functions

- Main execution function (required):

```c
emu_result_t block_<name>(block_handle_t block_data);
```

- Parser (only if the block needs custom parsing beyond inputs/outputs):

```c
emu_result_t block_<name>_parse(const uint8_t *packet_data, const uint16_t packet_len, void *block);
```

- Free function (only if the block allocates its own resources):

```c
void block_<name>_free(block_handle_t block);
```

- Verifier (only if the block has its own struct and needs verification):

```c
emu_result_t block_<name>_verify(block_handle_t block); // returns .code == EMU_OK when valid
```

## Provided API (helpers)

### Input / Output helpers

- Check if all connected inputs are updated:

```c
bool emu_block_check_inputs_updated(block_handle_t block);
```

- Check if a selected input is updated:

```c
bool block_in_updated(block_handle_t block, uint8_t num);
```

- Check if a selected input is updated and true (useful for enable/EN inputs):

```c
bool block_check_in_true(block_handle_t block, uint8_t num);
```

- Set provided variable to selected output instance of block (wrapper around `mem_set`). Returns `emu_result_t` on failure/success:

```c
emu_result_t block_set_output(block_handle_t block, mem_var_t var, uint8_t num);
```

- Reset selected block output instances status (if supported):

```c
void emu_block_reset_outputs_status(block_handle_t block);
```

### Memory / Instance getters and setters

- Low-level memory get (raw). If `by_reference` is true, the returned `emu_result_t` contains a pointer to the instance data:

```c
emu_result_t mem_get(mem_var_t *result, const mem_access_t *search, bool by_reference);
```

- Convenience macro `MEM_GET(dst_ptr, mem_access_t_ptr)` is provided to detect the destination type and attempt retrieval with any necessary casting/clamping. It returns an `emu_result_t` on failure.

- Low-level memory set (performs clamp/cast as required):

```c
emu_result_t mem_set(const mem_var_t source, const mem_access_t *target);
```

## Notes 

- If input used mutliple times then perform one read into temporary buffer to avoid repetition of function calls
- Feel free to perform operation without usage of mem_get/set when it's required convinient like array copy
- EN status check is reqiured when block doesn't require providing any other inputs 
- When block is connected only to global variables it and has no EN input it shall run each cycle as every input is always updated for global variables.
# core/ is grouped by layer -- errors, store, obj, block, loader -- but
# every include across the project is by bare filename ("vm_obj.h", not
# "obj/vm_obj.h"). That is deliberate: the grouping is for people reading the
# tree, and regrouping it again later should not touch a single #include line.
# Each group therefore goes on the include path rather than into the paths.
idf_component_register(
    SRCS "core/errors/vm_errors.c"
         "core/store/vm_store.c"
         "core/obj/vm_obj_build.c" "core/obj/vm_obj_access.c" "core/obj/vm_obj_dyn.c"
         "core/block/vm_block_build.c" "core/block/vm_block_run.c"
         "core/exec/vm_section.c" "core/exec/vm_event.c" "core/exec/vm_exec.c"
         "core/loader/vm_loader.c"
         "core/sub/vm_sub.c"
         "blocks/vm_blocks_table.c"
    INCLUDE_DIRS "core/errors" "core/store" "core/obj" "core/block" "core/exec" "core/loader" "core/sub" "blocks"
    # exec/ is the first layer with anything outside the arena to talk to: the
    # supervisor is a FreeRTOS task pinned to core 1 (utils' R_TASK_* macros),
    # and every timing block plus the block watchdog reads esp_timer.
    REQUIRES sys_errors utils freertos esp_timer callbacks
)

# The project builds at -Og (CONFIG_COMPILER_OPTIMIZATION_DEBUG), which is the
# right default for driver and bring-up code but the wrong one here: accessor
# resolution passes vm_payload_t and vm_resolved_t by value through several
# layers, and -Og turns every one of those into stack traffic instead of
# keeping them in registers. This is the one component that runs inside a scan
# cycle rather than at boot or on a packet, so it is compiled for speed
# regardless of the project-wide setting. Measured with vm_bench.c -- see the
# benchmark section of VM.MD before changing this.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)

# The palette is a designated-initialiser table (blocks/vm_blocks_table.c), and
# C lets a later designated initialiser silently override an earlier one -- so
# two block types claiming the same id would compile clean and the last one
# would win. -Woverride-init makes that a warning instead of a surprise. It is
# in -Wextra, which the project does not enable, so it is asked for by name.
target_compile_options(${COMPONENT_LIB} PRIVATE -Woverride-init)

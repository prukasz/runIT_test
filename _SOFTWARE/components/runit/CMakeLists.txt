file(GLOB SELFTEST_SRCS "${CMAKE_CURRENT_LIST_DIR}/selftest/*.c")

idf_component_register(SRCS "runit.c" "vm_bench.c" ${SELFTEST_SRCS}
                    INCLUDE_DIRS "include" "selftest"
                    REQUIRES freertos log devices sys_i2c sys_device sys_io sys_errors sys_power ble sys_interface codecs sys_actions VM)

# Matches the VM component's own -O2 (see components/VM/CMakeLists.txt). The
# benchmark's raw-C baseline lives in this file, so compiling it at the
# project's -Og while the code it measures runs at -O2 would understate every
# ratio -- the floor has to be built the same way as what it is a floor for.
set_source_files_properties(vm_bench.c PROPERTIES COMPILE_OPTIONS "-O2")

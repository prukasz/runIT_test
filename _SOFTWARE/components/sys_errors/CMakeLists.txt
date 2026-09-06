file(GLOB code_items RELATIVE ${CMAKE_CURRENT_LIST_DIR} "codes/*")
set(code_dirs "")
foreach(item ${code_items})
    if(IS_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/${item}")
        list(APPEND code_dirs "${item}")
    endif()
endforeach()

idf_component_register(
    SRCS "sys_error.c" "sys_error_handler.c"
    INCLUDE_DIRS "include" ${code_dirs} "codes"
                 "../system/sys_device/include"
                 "../system/sys_io/include"
                 "../system/sys_power/include"
                 "../system/sys_i2c/include"
                 "../system/ble/include"
                 "../system/sys_interface/include"
                 "../system/sys_buffers/include"
                 "../system/sys_actions/include"
                 # VM has its own component now, but it REQUIRES sys_errors, so a
                 # REQUIRES back here would be circular -- reach its error map header
                 # (sys_error_vm.h) as a raw path instead, same as codecs' encoder.
                 "../VM/core/errors"
                 # codecs is header-only and REQUIRES sys_errors, so pulling in the
                 # error encoder as a plain include dir avoids a component cycle.
                 "../codecs/encoders"
                 "../devices/devices/include"
    REQUIRES esp_ringbuf
    PRIV_REQUIRES utils ble
)
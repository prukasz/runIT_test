cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
    set(ENV{IDF_PATH} "C:/esp/v6.0.1/esp-idf")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Ignore the VM folder totally

# Point directly to the folders in your project root
list(APPEND EXTRA_COMPONENT_DIRS 
    "components/system"
    "components/devices"
    "components/runit"
    "components/utils"
    "components/sys_errors"
    "components/codecs"
)
project(runIT)

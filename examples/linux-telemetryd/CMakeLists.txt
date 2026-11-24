cmake_minimum_required(VERSION 3.16)
project(telemetryd C)

add_executable(telemetryd
    src/main.c
    src/sensors.c
    src/publish.c
    src/config.c)

target_compile_options(telemetryd PRIVATE -Wall -O2)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(telemetryd PRIVATE HAVE_WATCHDOG=1)
endif()

# glibc keeps clock_* and dlopen in separate libraries on older systems
target_link_libraries(telemetryd PRIVATE pthread rt -ldl)

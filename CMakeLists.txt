cmake_minimum_required(VERSION 3.16)
project(qport C)

# Host build:   cmake -B build && cmake --build build
# QNX OS 8.0:   source ~/qnx800/qnxsdp-env.sh
#               cmake -B build-qnx -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-x86_64.cmake
#               cmake --build build-qnx
# (cmake/qnx-*.cmake were written by `qport toolchain --out cmake src`.)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(qport
    src/main.c src/scan.c src/lexer.c src/rules.c
    src/report.c src/emit.c src/util.c src/html_template.c)

if(QNX)
  target_compile_definitions(qport PRIVATE _QNX_SOURCE)
else()
  target_compile_definitions(qport PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
endif()
target_compile_options(qport PRIVATE -Wall -Wextra)

install(TARGETS qport RUNTIME DESTINATION bin)

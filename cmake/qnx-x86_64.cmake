# QNX OS 8.0 CMake toolchain file (x86_64), written by qport 0.1.0.
#
#   source ~/qnx800/qnxsdp-env.sh
#   cmake -B build-qnx-x86_64 -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-x86_64.cmake
#   cmake --build build-qnx-x86_64

set(QNX TRUE)
set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(QNX_HOST "$ENV{QNX_HOST}")
set(QNX_TARGET "$ENV{QNX_TARGET}")
if(NOT QNX_HOST OR NOT QNX_TARGET)
  message(FATAL_ERROR "QNX_HOST / QNX_TARGET are not set: source qnxsdp-env.sh first")
endif()

set(CMAKE_C_COMPILER "${QNX_HOST}/usr/bin/qcc")
set(CMAKE_CXX_COMPILER "${QNX_HOST}/usr/bin/q++")
set(CMAKE_C_COMPILER_TARGET gcc_ntox86_64)
set(CMAKE_CXX_COMPILER_TARGET gcc_ntox86_64)
set(CMAKE_SYSROOT "${QNX_TARGET}")
set(CMAKE_FIND_ROOT_PATH "${QNX_TARGET}" "${QNX_TARGET}/x86_64")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Expose the full QNX API set (QNX Porting Guide: macro conflicts).
set(CMAKE_C_FLAGS_INIT "-D_QNX_SOURCE")
set(CMAKE_CXX_FLAGS_INIT "-D_QNX_SOURCE")

# qcc's default stack is far smaller than Linux's 8 MB. If qport reported
# QNX083 (large stack arrays), either fix the code or uncomment:
# string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " -Wl,-z,stack-size,8388608")

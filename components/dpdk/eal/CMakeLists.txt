cmake_minimum_required(VERSION 3.16)
project(eal_hello C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 自己找 DPDK，不依赖根文件
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)
include_directories(${DPDK_INCLUDE_DIRS})
link_directories(${DPDK_LIBRARY_DIRS})

add_executable(eal_hello main.c)
target_include_directories(eal_hello PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DPDK_INCLUDE_DIRS})
target_compile_options(eal_hello PRIVATE ${DPDK_CFLAGS})
target_compile_definitions(eal_hello PRIVATE ALLOW_EXPERIMENTAL_API)
target_link_libraries(eal_hello PRIVATE ${DPDK_LIBRARIES})

set_target_properties(eal_hello PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

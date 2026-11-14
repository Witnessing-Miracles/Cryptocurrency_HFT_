cmake_minimum_required(VERSION 3.16)
project(capture_packet C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

add_executable(capture_packet main.c)
target_include_directories(capture_packet PRIVATE ${DPDK_INCLUDE_DIRS})
target_compile_options(capture_packet PRIVATE ${DPDK_CFLAGS})
target_link_directories(capture_packet PRIVATE ${DPDK_LIBRARY_DIRS})
target_link_libraries(capture_packet PRIVATE ${DPDK_LIBRARIES})

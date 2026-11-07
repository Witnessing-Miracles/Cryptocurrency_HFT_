cmake_minimum_required(VERSION 3.16)
project(BinanceGatewayStarter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost 1.74 REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(Threads REQUIRED)

add_executable(binance_gateway src/main.cpp)
target_include_directories(binance_gateway PRIVATE include)
target_link_libraries(binance_gateway PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    Threads::Threads
)
target_compile_options(binance_gateway PRIVATE -Wall -Wextra -Wpedantic)

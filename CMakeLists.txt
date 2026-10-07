cmake_minimum_required(VERSION 3.16)
project(cn_binary_protocol LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)

add_library(cn_protocol
    src/protocol.cpp
    src/hex_dump.cpp
)
target_include_directories(cn_protocol PUBLIC include)

add_executable(observe src/server.cpp)
target_link_libraries(observe PRIVATE cn_protocol)

add_executable(bcurl src/client.cpp)
target_link_libraries(bcurl PRIVATE cn_protocol)

add_executable(protocol_tests tests/protocol_tests.cpp)
target_link_libraries(protocol_tests PRIVATE cn_protocol)

enable_testing()
add_test(NAME protocol_tests COMMAND protocol_tests)

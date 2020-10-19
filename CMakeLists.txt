CMAKE_MINIMUM_REQUIRED(VERSION 3.12)

PROJECT(netpipe)

add_compile_options(-O3)
add_compile_options(-ggdb3)
add_compile_options(-m64)
add_compile_options(-std=c++11)
add_compile_options(-Wall -Wextra -Wstrict-aliasing=3)
add_compile_options(-Wmissing-format-attribute)
add_compile_options(-Wpacked-bitfield-compat)
add_compile_options(-Wno-overflow)
add_compile_options(-Wno-parentheses)
add_compile_options(-Wno-long-long)
add_compile_options(-fno-strict-aliasing)
add_compile_options(-pthread)

INCLUDE_DIRECTORIES(include)

ADD_EXECUTABLE(np src/netpipe.cpp src/netpipe_server.cpp src/misc.cpp)
TARGET_LINK_LIBRARIES(np pthread)

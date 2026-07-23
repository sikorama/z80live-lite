cmake_minimum_required(VERSION 3.16)
project(rasm_lite CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

# Bibliothèques autonomes (portables WASM/JS)
add_library(z80 STATIC z80.cpp)
target_include_directories(z80 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(pp STATIC pp.cpp expr.cpp)
target_include_directories(pp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(parser STATIC parser.cpp)
target_include_directories(parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(parser PUBLIC z80)

add_library(sna STATIC sna.cpp)
target_include_directories(sna PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(asmlib STATIC asm.cpp)
target_include_directories(asmlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asmlib PUBLIC parser z80 pp sna)

# Outil -E : export de la source préprocessée
add_executable(ppdump pp_main.cpp)
target_link_libraries(ppdump PRIVATE pp)

# CLI bout-en-bout : .asm -> .bin
add_executable(rasmlite asm_main.cpp)
target_link_libraries(rasmlite PRIVATE asmlib)

# Tests
add_executable(z80_test z80_test.cpp)
target_link_libraries(z80_test PRIVATE z80)

add_executable(pp_test pp_test.cpp)
target_link_libraries(pp_test PRIVATE pp)

add_executable(parser_test parser_test.cpp)
target_link_libraries(parser_test PRIVATE parser)

add_executable(asm_test asm_test.cpp)
target_link_libraries(asm_test PRIVATE asmlib)

add_executable(sna_test sna_test.cpp)
target_link_libraries(sna_test PRIVATE sna)

enable_testing()
add_test(NAME z80_encoding COMMAND z80_test)
add_test(NAME preprocessor COMMAND pp_test)
add_test(NAME parser COMMAND parser_test)
add_test(NAME assembler COMMAND asm_test)
add_test(NAME sna COMMAND sna_test)

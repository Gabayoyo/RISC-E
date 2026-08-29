cmake_minimum_required(VERSION 3.10.0)
project(risc-e VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core library (everything except the CLI entry point)
file(GLOB_RECURSE CORE_SOURCES "src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${PROJECT_SOURCE_DIR}/src/main.cpp")

add_library(risc-e-core STATIC ${CORE_SOURCES})
target_include_directories(risc-e-core PUBLIC include)

add_executable(risc-e src/main.cpp)
target_link_libraries(risc-e PRIVATE risc-e-core)

include(CTest)
enable_testing()

# --- Tests ---

find_program(RISCV_GCC riscv64-unknown-elf-gcc)

add_executable(memory_fault_test tests/memory_fault_test.cpp)
target_link_libraries(memory_fault_test PRIVATE risc-e-core)
add_test(NAME memory_fault_test COMMAND memory_fault_test)

if(RISCV_GCC)
    set(TEST_PROGRAMS exit stack heap wild_read wild_write misaligned)

    foreach(prog ${TEST_PROGRAMS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/programs/${prog}.elf
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/programs
            COMMAND ${RISCV_GCC} -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles
                    -Wl,-Ttext=0x10000 -o ${CMAKE_CURRENT_BINARY_DIR}/programs/${prog}.elf
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/programs/${prog}.S
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/programs/${prog}.S
        )
    endforeach()

    add_custom_target(build_test_programs ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/programs/exit.elf
                ${CMAKE_CURRENT_BINARY_DIR}/programs/stack.elf
                ${CMAKE_CURRENT_BINARY_DIR}/programs/heap.elf
                ${CMAKE_CURRENT_BINARY_DIR}/programs/wild_read.elf
                ${CMAKE_CURRENT_BINARY_DIR}/programs/wild_write.elf
                ${CMAKE_CURRENT_BINARY_DIR}/programs/misaligned.elf
    )

    add_test(NAME exit COMMAND risc-e ${CMAKE_CURRENT_BINARY_DIR}/programs/exit.elf)
    set_tests_properties(exit PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 42")

    add_test(NAME stack COMMAND risc-e ${CMAKE_CURRENT_BINARY_DIR}/programs/stack.elf)
    set_tests_properties(stack PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 7")

    add_test(NAME heap COMMAND risc-e ${CMAKE_CURRENT_BINARY_DIR}/programs/heap.elf)
    set_tests_properties(heap PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 123")

    add_test(NAME wild_read COMMAND risc-e ${CMAKE_CURRENT_BINARY_DIR}/programs/wild_read.elf)
    set_tests_properties(wild_read PROPERTIES WILL_FAIL TRUE)

    add_test(NAME wild_write COMMAND risc-e ${CMAKE_CURRENT_BINARY_DIR}/programs/wild_write.elf)
    set_tests_properties(wild_write PROPERTIES WILL_FAIL TRUE)

    add_test(NAME misaligned COMMAND risc-e ${CMAKE_CURRENT_BINARY_DIR}/programs/misaligned.elf)
    set_tests_properties(misaligned PROPERTIES WILL_FAIL TRUE)
endif()
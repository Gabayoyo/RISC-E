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

find_program(RISCV_GCC NAMES riscv64-unknown-elf-gcc riscv64-elf-gcc)

add_executable(memory_fault_test tests/memory_fault_test.cpp)
target_link_libraries(memory_fault_test PRIVATE risc-e-core)
add_test(NAME memory_fault_test COMMAND memory_fault_test)

add_executable(branch_prediction_test tests/branch_prediction_test.cpp)
target_link_libraries(branch_prediction_test PRIVATE risc-e-core)
add_test(NAME branch_prediction_test COMMAND branch_prediction_test)

if(RISCV_GCC)
    set(TEST_PROGRAMS exit stack heap wild_read wild_write misaligned branches)
    set(PROGRAM_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/programs/src)
    set(PROGRAM_ELF_DIR ${CMAKE_CURRENT_BINARY_DIR}/programs/elf)

    foreach(prog ${TEST_PROGRAMS})
        add_custom_command(
            OUTPUT ${PROGRAM_ELF_DIR}/${prog}.elf
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PROGRAM_ELF_DIR}
            COMMAND ${RISCV_GCC} -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles
                    -Wl,-Ttext=0x10000 -o ${PROGRAM_ELF_DIR}/${prog}.elf
                    ${PROGRAM_SRC_DIR}/${prog}.S
            DEPENDS ${PROGRAM_SRC_DIR}/${prog}.S
        )
    endforeach()

    add_custom_target(build_test_programs ALL
        DEPENDS ${PROGRAM_ELF_DIR}/exit.elf
                ${PROGRAM_ELF_DIR}/stack.elf
                ${PROGRAM_ELF_DIR}/heap.elf
                ${PROGRAM_ELF_DIR}/wild_read.elf
                ${PROGRAM_ELF_DIR}/wild_write.elf
                ${PROGRAM_ELF_DIR}/misaligned.elf
                ${PROGRAM_ELF_DIR}/branches.elf
    )

    add_test(NAME exit COMMAND risc-e ${PROGRAM_ELF_DIR}/exit.elf)
    set_tests_properties(exit PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 42")

    add_test(NAME stack COMMAND risc-e ${PROGRAM_ELF_DIR}/stack.elf)
    set_tests_properties(stack PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 7")

    add_test(NAME heap COMMAND risc-e ${PROGRAM_ELF_DIR}/heap.elf)
    set_tests_properties(heap PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 123")

    add_test(NAME wild_read COMMAND risc-e ${PROGRAM_ELF_DIR}/wild_read.elf)
    set_tests_properties(wild_read PROPERTIES WILL_FAIL TRUE)

    add_test(NAME wild_write COMMAND risc-e ${PROGRAM_ELF_DIR}/wild_write.elf)
    set_tests_properties(wild_write PROPERTIES WILL_FAIL TRUE)

    add_test(NAME misaligned COMMAND risc-e ${PROGRAM_ELF_DIR}/misaligned.elf)
    set_tests_properties(misaligned PROPERTIES WILL_FAIL TRUE)

    add_test(NAME branches COMMAND risc-e ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(branches PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 7")

    add_test(NAME branch_stats COMMAND risc-e ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(branch_stats PROPERTIES PASS_REGULAR_EXPRESSION "hits: 5, misses: 2")

    add_test(NAME branch_stats_from_source COMMAND risc-e ${PROGRAM_SRC_DIR}/branches.S)
    set_tests_properties(branch_stats_from_source
                         PROPERTIES PASS_REGULAR_EXPRESSION "hits: 5, misses: 2")

    add_test(NAME branch_stats_gshare
             COMMAND risc-e --predictor gshare ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(branch_stats_gshare
                         PROPERTIES PASS_REGULAR_EXPRESSION "hits: 5, misses: 2")

    add_test(NAME branch_stats_tournament
             COMMAND risc-e --predictor tournament ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(branch_stats_tournament
                         PROPERTIES PASS_REGULAR_EXPRESSION "hits: 5, misses: 2")

    add_test(NAME branch_stats_always_not_taken
             COMMAND risc-e --predictor always-not-taken ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(branch_stats_always_not_taken
                         PROPERTIES PASS_REGULAR_EXPRESSION "hits: 2, misses: 5")
endif()
# Unit tests (no cross-compiler needed).

add_executable(memory_fault_test memory_fault_test.cpp)
target_link_libraries(memory_fault_test PRIVATE risc-e-core)
add_test(NAME memory_fault_test COMMAND memory_fault_test)

add_executable(branch_prediction_test branch_prediction_test.cpp)
target_link_libraries(branch_prediction_test PRIVATE risc-e-core)
add_test(NAME branch_prediction_test COMMAND branch_prediction_test)

add_executable(pipeline_test pipeline_test.cpp)
target_link_libraries(pipeline_test PRIVATE risc-e-core)
add_test(NAME pipeline_test COMMAND pipeline_test)

add_executable(harness_test harness_test.cpp)
target_link_libraries(harness_test PRIVATE risc-e-core)
add_test(NAME harness_test COMMAND harness_test)

add_executable(icache_test icache_test.cpp)
target_link_libraries(icache_test PRIVATE risc-e-core)
add_test(NAME icache_test COMMAND icache_test)

# Integration tests: RISC-V test programs compiled with a cross-compiler.
# When no RISC-V GCC is on PATH these tests are skipped.

find_program(RISCV_GCC NAMES riscv64-unknown-elf-gcc riscv64-elf-gcc)

if(RISCV_GCC)
    set(TEST_PROGRAMS exit stack heap wild_read wild_write misaligned branches)
    set(PROGRAM_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/programs/src)
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
    set_tests_properties(branch_stats PROPERTIES PASS_REGULAR_EXPRESSION "hits: 6")

    add_test(NAME branch_stats_from_source COMMAND risc-e ${PROGRAM_SRC_DIR}/branches.S)
    set_tests_properties(branch_stats_from_source
                         PROPERTIES PASS_REGULAR_EXPRESSION "hits: 6")

    add_test(NAME hello_c COMMAND risc-e ${PROGRAM_SRC_DIR}/hello.c)
    set_tests_properties(hello_c PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 42")

    add_test(NAME hello_c_output COMMAND risc-e ${PROGRAM_SRC_DIR}/hello.c)
    set_tests_properties(hello_c_output PROPERTIES PASS_REGULAR_EXPRESSION "Hello from C")

    add_test(NAME branchy_c COMMAND risc-e --predictor gshare ${PROGRAM_SRC_DIR}/branchy.c)
    set_tests_properties(branchy_c PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 46")

    add_test(NAME branchy_c_stats COMMAND risc-e ${PROGRAM_SRC_DIR}/branchy.c)
    set_tests_properties(branchy_c_stats
                         PROPERTIES PASS_REGULAR_EXPRESSION "branches: [0-9]+")

    # C programs with varying branch patterns
    add_test(NAME branch_loops_c COMMAND risc-e ${PROGRAM_SRC_DIR}/branch_loops.c)
    set_tests_properties(branch_loops_c PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 5050")

    add_test(NAME branch_data_c COMMAND risc-e ${PROGRAM_SRC_DIR}/branch_data.c)
    set_tests_properties(branch_data_c PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 10")

    add_test(NAME branch_data_c_stats COMMAND risc-e ${PROGRAM_SRC_DIR}/branch_data.c)
    set_tests_properties(branch_data_c_stats
                         PROPERTIES PASS_REGULAR_EXPRESSION "branches: [0-9]+")

    add_test(NAME branch_nested_c COMMAND risc-e ${PROGRAM_SRC_DIR}/branch_nested.c)
    set_tests_properties(branch_nested_c PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 36")

    add_test(NAME branch_switch_c COMMAND risc-e ${PROGRAM_SRC_DIR}/branch_switch.c)
    set_tests_properties(branch_switch_c PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 144")

    add_test(NAME branch_calls_c COMMAND risc-e ${PROGRAM_SRC_DIR}/branch_calls.c)
    set_tests_properties(branch_calls_c PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 55")

    add_test(NAME branch_calls_c_stats COMMAND risc-e ${PROGRAM_SRC_DIR}/branch_calls.c)
    set_tests_properties(branch_calls_c_stats
                         PROPERTIES PASS_REGULAR_EXPRESSION "branches: [0-9]+")

    # General-purpose C programs, reusable for other emulator testing
    add_test(NAME general_memory_c COMMAND risc-e ${PROGRAM_SRC_DIR}/general_memory.c)
    set_tests_properties(general_memory_c PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 108")

    add_test(NAME general_arithmetic_c COMMAND risc-e ${PROGRAM_SRC_DIR}/general_arithmetic.c)
    set_tests_properties(general_arithmetic_c PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 9")

    add_test(NAME general_heap_c COMMAND risc-e ${PROGRAM_SRC_DIR}/general_heap.c)
    set_tests_properties(general_heap_c PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 224")

    add_test(NAME branch_stats_gshare
             COMMAND risc-e --predictor gshare ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(branch_stats_gshare
                         PROPERTIES PASS_REGULAR_EXPRESSION "hits: 6")

    add_test(NAME branch_stats_tournament
             COMMAND risc-e --predictor tournament ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(branch_stats_tournament
                         PROPERTIES PASS_REGULAR_EXPRESSION "hits: 6")

    add_test(NAME branch_stats_ras
             COMMAND risc-e --predictor ras ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(branch_stats_ras
                         PROPERTIES PASS_REGULAR_EXPRESSION "hits: 3")

    add_test(NAME branch_stats_always_not_taken
             COMMAND risc-e --predictor always-not-taken ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(branch_stats_always_not_taken
                         PROPERTIES PASS_REGULAR_EXPRESSION "hits: 2")

    add_test(NAME comparison_all_
             COMMAND risc-e --comparison predictor ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(comparison_all_
                         PROPERTIES PASS_REGULAR_EXPRESSION "two-bit")

    add_test(NAME comparison_cycles
             COMMAND risc-e --comparison predictor ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(comparison_cycles
                         PROPERTIES PASS_REGULAR_EXPRESSION "two-bit.*1.44x")

    add_test(NAME comparison_requires_name
             COMMAND risc-e --comparison ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(comparison_requires_name PROPERTIES WILL_FAIL TRUE)

    add_test(NAME comparison_single_predictor
             COMMAND risc-e --comparison gshare ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(comparison_single_predictor
                         PROPERTIES PASS_REGULAR_EXPRESSION "gshare"
                                    FAIL_REGULAR_EXPRESSION "two-bit")

    add_test(NAME branch_stats_params
             COMMAND risc-e --predictor gshare --param gshare.history-bits=4
                     --param gshare.ras-depth=0 ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(branch_stats_params
                         PROPERTIES PASS_REGULAR_EXPRESSION "hits: 5")

    add_test(NAME pipeline_stats COMMAND risc-e ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(pipeline_stats PROPERTIES PASS_REGULAR_EXPRESSION "pipeline")

    add_test(NAME icache_report COMMAND risc-e ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(icache_report
                         PROPERTIES PASS_REGULAR_EXPRESSION "basic blocks: 5")

    add_test(NAME icache_report_numbers COMMAND risc-e ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(icache_report_numbers
                         PROPERTIES PASS_REGULAR_EXPRESSION "instructions executed: 16")

    add_test(NAME icache_comparison COMMAND risc-e --comparison icache
             ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(icache_comparison
                         PROPERTIES PASS_REGULAR_EXPRESSION
                                     "icache-fa.*3.59x|icache-plru.*3.59x")

    add_test(NAME icache_cache COMMAND risc-e ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(icache_cache
                         PROPERTIES PASS_REGULAR_EXPRESSION "cycles saved: 300 \\(72.12%\\)")

    add_test(NAME icache_cache_param COMMAND risc-e --param icache-fa.miss-penalty=100
             ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(icache_cache_param
                         PROPERTIES PASS_REGULAR_EXPRESSION "miss stalls: 200")

    add_test(NAME icache_eviction
             COMMAND risc-e --param icache-fa.ways=1
                     ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(icache_eviction
                         PROPERTIES PASS_REGULAR_EXPRESSION "evictions: 1")

    add_test(NAME icache_select COMMAND risc-e --icache icache-plru
             ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(icache_select
                         PROPERTIES PASS_REGULAR_EXPRESSION "PLRU")

    add_test(NAME icache_select_unknown COMMAND risc-e --icache icache-nope
             ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(icache_select_unknown PROPERTIES WILL_FAIL TRUE)

    add_test(NAME pipeline_penalty_5stage
             COMMAND risc-e ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(pipeline_penalty_5stage
                         PROPERTIES PASS_REGULAR_EXPRESSION "stall cycles: 2")

    add_test(NAME pipeline_penalty_10stage
             COMMAND risc-e --pipeline-stages 10 ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(pipeline_penalty_10stage
                         PROPERTIES PASS_REGULAR_EXPRESSION "stall cycles: 7")

    add_test(NAME pipeline_penalty_override
             COMMAND risc-e --stall-penalty 4 ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(pipeline_penalty_override
                         PROPERTIES PASS_REGULAR_EXPRESSION "stall cycles: 4")

    add_test(NAME mismatched_param_rejected
             COMMAND risc-e --predictor two-bit --param gshare.history-bits=8
                     ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(mismatched_param_rejected PROPERTIES WILL_FAIL TRUE)

    add_test(NAME unknown_param_rejected
             COMMAND risc-e --predictor two-bit --param two-bit.history-bits=8
                     ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(unknown_param_rejected PROPERTIES WILL_FAIL TRUE)

    add_test(NAME list_ COMMAND risc-e --list-)
    set_tests_properties(list_ PROPERTIES PASS_REGULAR_EXPRESSION "history-bits=12")

    add_test(NAME list_components COMMAND risc-e --list)
    set_tests_properties(list_components PROPERTIES PASS_REGULAR_EXPRESSION "pipeline:")

    add_test(NAME pipeline_param COMMAND risc-e --param pipeline.stages=10
             ${PROGRAM_ELF_DIR}/branches.elf)
    set_tests_properties(pipeline_param PROPERTIES PASS_REGULAR_EXPRESSION "stall cycles: 7")

    add_test(NAME list_predictor_detail COMMAND risc-e --list- gshare)
    set_tests_properties(list_predictor_detail
                         PROPERTIES PASS_REGULAR_EXPRESSION "pattern-history")
endif()

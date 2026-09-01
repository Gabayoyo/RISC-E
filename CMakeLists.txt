cmake_minimum_required(VERSION 3.10.0)
project(risc-e VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# Core library (everything except the CLI entry point)
file(GLOB_RECURSE CORE_SOURCES "src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${PROJECT_SOURCE_DIR}/src/main.cpp")

add_library(risc-e-core STATIC ${CORE_SOURCES})
target_include_directories(risc-e-core PUBLIC include)

add_executable(risc-e src/main.cpp)
target_link_libraries(risc-e PRIVATE risc-e-core)
# The CLI's default JSON-report directory lives at the project root, so it
# works no matter where the binary is invoked from.
target_compile_definitions(risc-e PRIVATE RISC_E_PROJECT_ROOT="${PROJECT_SOURCE_DIR}")

include(CTest)
enable_testing()

# All test targets and their registration live in tests/.
add_subdirectory(tests)

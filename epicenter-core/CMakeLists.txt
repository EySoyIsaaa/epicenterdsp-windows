cmake_minimum_required(VERSION 3.20)
project(EpicenterDSPCore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Compiler options for strict IEEE float and warnings
if(MSVC)
    add_compile_options(/W4 /fp:precise /EHsc /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# -----------------------------------------------------------------------------
# 1. Epicenter DSP Core Library (Pure, platform-agnostic C++ engine)
# -----------------------------------------------------------------------------
add_library(epicenter_core STATIC
    include/EpicenterDSP.h
    src/EpicenterDSP.cpp
    src/internal/EpicenterProcessor.hpp
    src/internal/EpicenterProcessor.cpp
    src/internal/DspPrimitives.hpp
)

target_include_directories(epicenter_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(epicenter_core PUBLIC EPICENTER_CORE_STATIC)

# -----------------------------------------------------------------------------
# 2. Frozen Legacy Reference Core (for regression & baseline oracle)
# -----------------------------------------------------------------------------
add_library(epicenter_legacy_reference STATIC
    tests/reference/legacy/EpicenterDSPCore.cpp
    tests/reference/legacy/EpicenterDSPCore.hpp
)
target_include_directories(epicenter_legacy_reference PUBLIC tests/reference/legacy)

# -----------------------------------------------------------------------------
# 3. Test Support Library
# -----------------------------------------------------------------------------
add_library(epicenter_test_support INTERFACE)
target_include_directories(epicenter_test_support INTERFACE tests/support)

# -----------------------------------------------------------------------------
# 4. Utilities: Fixture & Golden Reference Generators
# -----------------------------------------------------------------------------
add_executable(generate_fixtures tools/generate_fixtures.cpp)
target_link_libraries(generate_fixtures PRIVATE epicenter_test_support)

add_executable(generate_golden tools/generate_golden.cpp)
target_link_libraries(generate_golden PRIVATE epicenter_test_support epicenter_legacy_reference)

# -----------------------------------------------------------------------------
# 5. Core Test Suite
# -----------------------------------------------------------------------------
add_executable(epicenter_test_suite
    tests/main_test.cpp
    tests/test_initialization.cpp
    tests/test_processing.cpp
    tests/test_bypass.cpp
    tests/test_intensity.cpp
    tests/test_reset.cpp
    tests/test_safety.cpp
    tests/test_allocations.cpp
    tests/test_stability.cpp
    tests/test_regression.cpp
    tests/support/AllocCounter.cpp
)

target_link_libraries(epicenter_test_suite
    PRIVATE
        epicenter_core
        epicenter_test_support
)

enable_testing()
add_test(NAME CoreTestSuite COMMAND epicenter_test_suite)

# -----------------------------------------------------------------------------
# 6. Benchmark Suite (Compares Legacy Core vs New Epicenter Core)
# -----------------------------------------------------------------------------
add_executable(core_benchmark benchmarks/core_benchmark.cpp)
target_compile_definitions(core_benchmark PRIVATE EPICENTER_BENCH_WITH_NEW_CORE)
target_link_libraries(core_benchmark
    PRIVATE
        epicenter_core
        epicenter_test_support
        epicenter_legacy_reference
)

cmake_minimum_required(VERSION 3.20)
project(EpicenterAPO VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /fp:precise /EHsc /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core static library
add_library(epicenter_core STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/src/EpicenterDSP.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/src/internal/EpicenterProcessor.cpp"
)
target_include_directories(epicenter_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/src"
)
target_compile_definitions(epicenter_core PUBLIC EPICENTER_CORE_STATIC)

# Frozen Legacy Reference Core (for regression testing)
add_library(epicenter_legacy_reference STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/tests/reference/legacy/EpicenterDSPCore.cpp"
)
target_include_directories(epicenter_legacy_reference PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/tests/reference/legacy"
)

# 1. Epicenter APO Shared Library (DLL)
add_library(EpicenterAPO SHARED
    src/EpicenterApo.cpp
    src/ClassFactory.cpp
    src/DllMain.cpp
    src/EpicenterAPO.def
)
target_include_directories(EpicenterAPO
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/include
)
target_link_libraries(EpicenterAPO
    PRIVATE
        epicenter_core
        ole32
        oleaut32
        uuid
        propsys
        advapi32
)

# 2. APO Manager CLI Tool
add_executable(apo_manager
    tools/apo_manager.cpp
)
target_include_directories(apo_manager
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(apo_manager
    PRIVATE
        ole32
        oleaut32
        uuid
        advapi32
        propsys
)

# 3. Test Support & Test Suite
add_executable(epicenter_apo_test_suite
    tests/main_test.cpp
    tests/test_apo_initialization.cpp
    tests/test_apo_audio.cpp
    tests/test_apo_bypass.cpp
    tests/test_apo_ipc.cpp
    tests/test_apo_ab_crossfade.cpp
    tests/test_apo_realtime.cpp
    tests/test_apo_regression.cpp
    tests/test_apo_stress.cpp
    tests/test_apo_lifecycle_and_resilience.cpp
    src/EpicenterApo.cpp
    src/ClassFactory.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/tests/support/AllocCounter.cpp"
)
target_include_directories(epicenter_apo_test_suite
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/tests/support
        ${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/tests/reference/legacy
)
target_link_libraries(epicenter_apo_test_suite
    PRIVATE
        epicenter_core
        epicenter_legacy_reference
        ole32
        oleaut32
        uuid
        advapi32
        propsys
)

enable_testing()
add_test(NAME ApoTestSuite COMMAND epicenter_apo_test_suite)

# 4. Benchmark Executable
add_executable(apo_benchmark
    benchmarks/apo_benchmark.cpp
    src/EpicenterApo.cpp
    src/ClassFactory.cpp
)
target_include_directories(apo_benchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/tests/support
)
target_link_libraries(apo_benchmark
    PRIVATE
        epicenter_core
        ole32
        oleaut32
        uuid
        advapi32
        propsys
)

# 5. Layer Breakdown Benchmark
add_executable(layer_breakdown_benchmark
    benchmarks/layer_breakdown_benchmark.cpp
    src/EpicenterApo.cpp
    src/ClassFactory.cpp
)
target_include_directories(layer_breakdown_benchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-core/tests/support
)
target_link_libraries(layer_breakdown_benchmark
    PRIVATE
        epicenter_core
        ole32
        oleaut32
        uuid
        advapi32
        propsys
)

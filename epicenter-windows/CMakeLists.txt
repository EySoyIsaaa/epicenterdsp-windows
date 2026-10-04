cmake_minimum_required(VERSION 3.20)

project(EpicenterDSPWindows VERSION 1.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EPICENTER_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../epicenter-lab")

add_executable(EpicenterDSPWindows WIN32
    src/main.cpp
    src/AudioBridge.cpp
    src/AudioBridge.hpp
    src/PolicyConfig.hpp
    src/resource.h
    src/EpicenterWindows.rc
    "${EPICENTER_CORE_DIR}/EpicenterDSPCore.cpp"
    "${EPICENTER_CORE_DIR}/EpicenterDSPCore.hpp"
)

set_target_properties(EpicenterDSPWindows PROPERTIES OUTPUT_NAME "EpicenterDSP Windows")

target_include_directories(EpicenterDSPWindows PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<$<COMPILE_LANGUAGE:CXX>:${EPICENTER_CORE_DIR}>
)

target_compile_definitions(EpicenterDSPWindows PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A00000C
)

if(MINGW)
    target_compile_options(EpicenterDSPWindows PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(EpicenterDSPWindows PRIVATE -static -static-libgcc -static-libstdc++ -municode)
endif()

target_link_libraries(EpicenterDSPWindows PRIVATE
    ole32
    uuid
    propsys
    ksuser
    shell32
    dwmapi
    gdiplus
    gdi32
    user32
)

include(CTest)
if(BUILD_TESTING)
    add_executable(epicenter_dsp_tests
        tests/dsp_tests.cpp
        "${EPICENTER_CORE_DIR}/EpicenterDSPCore.cpp"
    )
    target_include_directories(epicenter_dsp_tests PRIVATE "${EPICENTER_CORE_DIR}")
    target_compile_definitions(epicenter_dsp_tests PRIVATE NOMINMAX)
    if(MINGW)
        target_compile_options(epicenter_dsp_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(epicenter_dsp_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME dsp_core COMMAND epicenter_dsp_tests)

    add_executable(policy_config_tests tests/policy_config_tests.cpp)
    target_include_directories(policy_config_tests PRIVATE src)
    target_compile_definitions(policy_config_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
        target_compile_options(policy_config_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(policy_config_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    target_link_libraries(policy_config_tests PRIVATE ole32 uuid)
    add_test(NAME policy_config_activation COMMAND policy_config_tests)
endif()

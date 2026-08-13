cmake_minimum_required(VERSION 3.20)

project(
  Northstar64
  VERSION 0.1.0
  DESCRIPTION "A deterministic RV64 machine and Unix-like kernel laboratory"
  LANGUAGES CXX
)

option(NORTHSTAR64_BUILD_TESTS "Build the Northstar64 test suite" ON)
option(NORTHSTAR64_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NORTHSTAR64_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(
  northstar64_core
  src/cpu.cpp
  src/csr.cpp
  src/decode.cpp
  src/elf.cpp
  src/machine.cpp
  src/memory.cpp
  src/privilege.cpp
  src/sv39.cpp
  src/trace.cpp
  src/trap.cpp
  src/uart.cpp
)

target_include_directories(
  northstar64_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(northstar64_core PUBLIC cxx_std_20)

function(northstar64_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(NORTHSTAR64_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(
      ${target}
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wold-style-cast
    )
    if(NORTHSTAR64_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

northstar64_set_warnings(northstar64_core)

if(NORTHSTAR64_ENABLE_SANITIZERS AND NOT MSVC)
  target_compile_options(northstar64_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(northstar64_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(northstar64 src/main.cpp)
target_link_libraries(northstar64 PRIVATE northstar64_core)
northstar64_set_warnings(northstar64)

include(GNUInstallDirs)
install(TARGETS northstar64 northstar64_core EXPORT Northstar64Targets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(NORTHSTAR64_BUILD_TESTS)
  enable_testing()
  add_executable(
    northstar64_tests
    tests/test_main.cpp
    tests/test_cpu.cpp
    tests/test_csr.cpp
    tests/test_decode.cpp
    tests/test_elf.cpp
    tests/test_memory.cpp
    tests/test_properties.cpp
    tests/test_sv39.cpp
    tests/test_trace.cpp
    tests/test_uart.cpp
  )
  target_include_directories(northstar64_tests PRIVATE tests)
  target_link_libraries(northstar64_tests PRIVATE northstar64_core)
  northstar64_set_warnings(northstar64_tests)
  add_test(NAME northstar64_tests COMMAND northstar64_tests)
endif()

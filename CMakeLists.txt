cmake_minimum_required(VERSION 3.20)

project(ecs_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

find_package(doctest CONFIG REQUIRED)

add_library(ecs_lab INTERFACE)
target_include_directories(ecs_lab
  INTERFACE
    include
)

add_library(ecs_lab_headers OBJECT
  src/ecs_lab_headers.cpp
)
target_include_directories(ecs_lab_headers
  PRIVATE
    include
)

add_executable(ecs_lab_tests
  tests/test_ecs_lab.cpp
)
target_link_libraries(ecs_lab_tests
  PRIVATE
    ecs_lab
    doctest::doctest
)
add_test(NAME ecs_lab_tests COMMAND ecs_lab_tests)

add_executable(ecs_lab_bench
  tests/bench_signature.cpp
)
target_link_libraries(ecs_lab_bench
  PRIVATE
    ecs_lab
)

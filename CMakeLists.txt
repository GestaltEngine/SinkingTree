cmake_minimum_required(VERSION 3.19)
find_package(Catch2 REQUIRED)

project(concurrent_map)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

include_directories(headers)

add_subdirectory(tests)
add_subdirectory(headers)

cmake_minimum_required(VERSION 3.0.0)
project(cgo VERSION 0.1.0)

set(CMAKE_CXX_COMPILE "g++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -fcoroutines -pthread")
set(CMAKE_BUILD_TYPE "debug")

include(CTest)
enable_testing()

add_subdirectory("src/core")
add_subdirectory("src/aio")
add_subdirectory("src/util")
add_subdirectory("test")

add_test("async-test" async_test)
add_test("yield-test" yield_test)
add_test("channel-test" channel_test)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

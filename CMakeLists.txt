cmake_minimum_required(VERSION 3.0.0)
project(cgo VERSION 0.1.0)

set(CMAKE_CXX_COMPILE "g++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++20 -fcoroutines -pthread")
set(CMAKE_BUILD_TYPE "debug")

include(CTest)
enable_testing()

add_subdirectory("src/core")
add_subdirectory("src/util")
add_subdirectory("src/aio")
add_subdirectory("test")

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

add_test("coroutine-test" coroutine_test)
add_test("schedule-test" schedule_test)
add_test("condition-test" condition_test)
add_test("channel-test" channel_test)
add_test("timer-test" timer_test)
add_test("select-test" select_test)
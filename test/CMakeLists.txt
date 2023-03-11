include_directories("../src")

add_executable(async_test async_test.cpp)
target_link_libraries(async_test cgo_core)

add_executable(yield_test yield_test.cpp)
target_link_libraries(yield_test cgo_core cgo_util)
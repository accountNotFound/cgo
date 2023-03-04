include_directories("../src")

add_executable(async_test async_test.cpp)
target_link_libraries(async_test cgo_core)
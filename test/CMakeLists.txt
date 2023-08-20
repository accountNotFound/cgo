include_directories("../src")

add_executable(async_test async_test.cpp)
target_link_libraries(async_test cgo_core)

add_executable(yield_test yield_test.cpp)
target_link_libraries(yield_test cgo_core cgo_util)

add_executable(channel_test channel_test.cpp)
target_link_libraries(channel_test cgo_core cgo_util)

add_executable(aio_test aio_test.cpp)
target_link_libraries(aio_test cgo_aio cgo_core cgo_util)

add_executable(aio_server aio_server.cpp)
target_link_libraries(aio_server cgo_aio cgo_core cgo_util)

add_executable(aio_client aio_client.cpp)
target_link_libraries(aio_client cgo_aio cgo_core cgo_util)
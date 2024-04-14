include_directories("../src")

add_executable(coroutine_test coroutine_test.cpp)
target_link_libraries(coroutine_test cgo_core)

add_executable(schedule_test schedule_test.cpp)
target_link_libraries(schedule_test cgo_core)

add_executable(condition_test condition_test.cpp)
target_link_libraries(condition_test cgo_core)

add_executable(channel_test channel_test.cpp)
target_link_libraries(channel_test cgo_core)

add_executable(timer_test timer_test.cpp)
target_link_libraries(timer_test cgo_core)

add_executable(select_test select_test.cpp)
target_link_libraries(select_test cgo_core)

# add_executable(socket_test socket_test.cpp)
# target_link_libraries(socket_test cgo_core cgo_aio)
include_directories(${PROJECT_SOURCE_DIR}/test ${PROJECT_SOURCE_DIR}/src/include)

file(GLOB unit_test_files ${PROJECT_SOURCE_DIR}/test/unit_test/*_test.cpp)

foreach(filepath ${unit_test_files})
  string(REGEX REPLACE ".+/(.+)\\..*" "\\1" filename ${filepath})
  string(REGEX REPLACE ".cpp" "" test_name ${filename})
  message(STATUS "get unit test: " ${test_name})
  add_executable(${test_name} ${filepath})
  target_link_libraries(${test_name} ${PROJECT_NAME})
  add_test(${test_name} ${test_name})  
endforeach()
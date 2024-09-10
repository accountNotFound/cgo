include_directories(${PROJECT_SOURCE_DIR}/src/include)

aux_source_directory(${PROJECT_SOURCE_DIR}/src/core libcgo_core)
add_library(libcgo ${libcgo_core})
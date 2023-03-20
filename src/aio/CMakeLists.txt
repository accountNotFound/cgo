include_directories(".")
include_directories("..")

aux_source_directory("." src)

add_library(cgo_aio ${src})
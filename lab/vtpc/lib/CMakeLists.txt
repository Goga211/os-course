add_library(vtpc SHARED vtpc.c)

target_include_directories(vtpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(vtpc PUBLIC c_std_11)

target_compile_options(vtpc PRIVATE -Wall -Wextra)

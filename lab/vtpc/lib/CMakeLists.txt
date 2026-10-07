add_library(vtpc SHARED vtpc.c)
add_library(vtpc_ebpf SHARED vtpc_ebpf.c)

target_include_directories(vtpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(vtpc_ebpf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(vtpc PUBLIC c_std_11)
target_compile_features(vtpc_ebpf PUBLIC c_std_11)

target_compile_options(vtpc PRIVATE -Wall -Wextra)
target_compile_options(vtpc_ebpf PRIVATE -Wall -Wextra -g)

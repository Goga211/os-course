add_executable(vtsh main.c)
target_include_directories(vtsh PUBLIC ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(vtsh PRIVATE libvtsh)
target_compile_features(vtsh PUBLIC c_std_11)

add_executable(proc-fork ${CMAKE_SOURCE_DIR}/progs/proc-fork.c)
target_include_directories(proc-fork PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_compile_features(proc-fork PUBLIC c_std_11)

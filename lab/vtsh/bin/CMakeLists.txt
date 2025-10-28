add_executable(vtsh main.c)
target_include_directories(vtsh PUBLIC ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(vtsh PRIVATE libvtsh)
target_compile_features(vtsh PUBLIC c_std_11)

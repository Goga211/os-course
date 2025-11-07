add_executable(vtsh main.c)
target_include_directories(vtsh PUBLIC ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(vtsh PRIVATE libvtsh)
target_compile_features(vtsh PUBLIC c_std_11)

add_executable(cpu-factorize ${CMAKE_SOURCE_DIR}/progs/cpu-factorize.c)
target_include_directories(cpu-factorize PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_compile_features(cpu-factorize PUBLIC c_std_11)

add_executable(ema-join-nl ${CMAKE_SOURCE_DIR}/progs/ema-join-nl.c)
target_include_directories(ema-join-nl PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_compile_features(ema-join-nl PUBLIC c_std_11)

add_executable(gen-join ${CMAKE_SOURCE_DIR}/progs/gen-join.c)
target_include_directories(gen-join PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_compile_features(gen-join PUBLIC c_std_11)

add_executable(io-load ${CMAKE_SOURCE_DIR}/progs/io-load.c)
target_include_directories(io-load PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_compile_features(io-load PUBLIC c_std_11)
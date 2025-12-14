add_library(
    libvtsh
    STATIC
    vtsh.c
    exec_cmd.c
    parse.c
    string_utils.c
)

target_include_directories(
    libvtsh
    PUBLIC
    ${CMAKE_SOURCE_DIR}/lib
)

target_compile_features(libvtsh PUBLIC c_std_11)

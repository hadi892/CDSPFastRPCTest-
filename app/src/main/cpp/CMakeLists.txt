# CMakeLists.txt for CDSPFastRPCTest JNI Native Loader
cmake_minimum_required(VERSION 3.22.1)

project("cdsp_loader")

# Compile native loader library
add_library(
    cdsp_loader
    SHARED
    native_loader.cpp
)

# Search for standard Android NDK logging and dynamic linker libraries
find_library(log-lib log)

# Link log and dl (for dynamic loading of Qualcomm vendor libraries)
target_link_libraries(
    cdsp_loader
    ${log-lib}
    dl
)

# Target ARM64-v8a optimizations
target_compile_options(cdsp_loader PRIVATE -Wall -Wextra -O2 -fPIC)

cmake_minimum_required(VERSION 3.15)
project(video_transcoders LANGUAGES CXX)

# C++ Standard configuration
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Python and Pybind11 dependencies
find_package(Python 3.8 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

# Find FFmpeg development packages via PkgConfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(AV REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavfilter
    libswscale
    libswresample
    libavutil
)

# Define the pybind11 shared module using your source files
pybind11_add_module(video_transcoders_ext MODULE src/bindings.cpp src/transformer_v12.cpp)

# Link FFmpeg libraries to the compiled module
target_link_libraries(video_transcoders_ext PRIVATE
    PkgConfig::AV
)

# Install rule for scikit-build-core to package the built binary
install(TARGETS video_transcoders_ext DESTINATION .)


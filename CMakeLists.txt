cmake_minimum_required(VERSION 3.15)
project(video_transcoders LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python 3.8 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AV REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavfilter
    libswscale
    libswresample
    libavutil
)

pybind11_add_module(video_transcoders_ext MODULE src/bindings.cpp src/transformer_v12.cpp)

target_link_libraries(video_transcoders_ext PRIVATE PkgConfig::AV)

install(TARGETS video_transcoders_ext DESTINATION .)

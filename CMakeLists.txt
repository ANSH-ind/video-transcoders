cmake_minimum_required(VERSION 3.15)
project(video_transcoders LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(pybind11 CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET 
    libavformat 
    libavcodec 
    libswscale 
    libavfilter 
    libavutil
)

pybind11_add_module(video_transcoders 
    src/bindings.cpp 
    src/transformer_v12.cpp
)

target_link_libraries(video_transcoders PRIVATE PkgConfig::FFMPEG)


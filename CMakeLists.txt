cmake_minimum_required(VERSION 3.18)

project(transformer_v12 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_package(pybind11 REQUIRED)


find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavfilter
    libswscale
    libswresample
    libavutil
)


pybind11_add_module(
    transcoders
    src/bindings.cpp
    src/transformer_v12.cpp
)


target_include_directories(
    transcoders
    PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)


target_link_libraries(
    transcoders
    PRIVATE
    ${FFMPEG_LIBRARIES}
    pthread
)

install(
    TARGETS transcoders
    LIBRARY DESTINATION .
)



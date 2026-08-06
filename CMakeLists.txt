cmake_minimum_required(VERSION 3.15)
project(video_transcoders LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(pybind11 CONFIG REQUIRED)

# Android ke liye PkgConfig use karna mushkil hota hai, isliye hum OS check laga rahe hain
if(NOT ANDROID)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET 
        libavformat 
        libavcodec 
        libswscale 
        libavfilter 
        libavutil
    )
endif()

pybind11_add_module(video_transcoders 
    src/bindings.cpp 
    src/transformer_v12.cpp
)

if(NOT ANDROID)
    target_link_libraries(video_transcoders PRIVATE PkgConfig::FFMPEG)
else()
    # Android ke liye pre-compiled FFmpeg libraries yahan link hongi
    # target_link_libraries(video_transcoders PRIVATE avformat avcodec swscale avfilter avutil)
endif()

# YEH SABSE IMPORTANT LINE HAI: Iske bina wheel khali banegi
install(TARGETS video_transcoders DESTINATION .)

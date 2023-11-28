cmake_minimum_required(VERSION 3.23)
project(OS_Project_MPZ)

set(CMAKE_CXX_STANDARD 17)

if (APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for Mac OS X" FORCE)
endif ()

add_executable(OS_Project_MPZ server.cpp)
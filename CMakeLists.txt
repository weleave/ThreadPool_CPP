cmake_minimum_required(VERSION 3.15)
project(ThreadPoolCPP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(threadpool_demo
    src/main.cpp
)

target_include_directories(threadpool_demo PRIVATE include)


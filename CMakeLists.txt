cmake_minimum_required(VERSION 3.28)
project(MazeGame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SFML 3.1.0 COMPONENTS Graphics Window System REQUIRED)

add_executable(maze main.cpp)
target_link_libraries(maze PRIVATE SFML::Graphics SFML::Window SFML::System)

if(MSVC)
    target_compile_options(maze PRIVATE /utf-8)   # để chuỗi tiếng Việt trong mã nguồn đúng UTF-8
endif()

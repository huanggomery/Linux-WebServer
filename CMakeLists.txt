cmake_minimum_required(VERSION 3.0)
project(HttpServer)

set(CMAKE_CXX_STANDARD 11)

file(GLOB_RECURSE SOURCES src/*.cpp)
include_directories(include)

add_executable(HttpServer ${SOURCES})

target_link_libraries(HttpServer pthread)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast")

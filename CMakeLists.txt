cmake_minimum_required(VERSION 3.20)

project(SourceSwitcher)

set(CMAKE_CXX_STANDARD 17)

add_executable(
    SourceSwitcher
    src/main.cpp
    src/SourceSwitcher.cpp)

find_package(PkgConfig REQUIRED)
pkg_search_module(GST REQUIRED gstreamer-1.0)

target_link_libraries(SourceSwitcher PUBLIC 
    ${GST_LIBRARIES}
)
target_include_directories(SourceSwitcher PRIVATE 
    ${GST_INCLUDE_DIRS}
)
set_target_properties(SourceSwitcher PROPERTIES CXX_STANDARD 17)
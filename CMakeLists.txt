cmake_minimum_required(VERSION 3.0)

message(STATUS ${CMAKE_BINARY_DIR})

project(trihexor)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_BINARY_DIR}")

find_package(opengl_system REQUIRED)
find_package(glfw3 REQUIRED)
find_package(imgui REQUIRED)

add_executable(trihexor
    src/trihexor.cpp
    "${imgui_RES_DIRS}/bindings/imgui_impl_glfw.cpp"
    "${imgui_RES_DIRS}/bindings/imgui_impl_glfw.h"
    "${imgui_RES_DIRS}/bindings/imgui_impl_opengl3.cpp"
    "${imgui_RES_DIRS}/bindings/imgui_impl_opengl3.h"
    )
target_link_libraries(trihexor PRIVATE imgui::imgui glfw::glfw opengl::opengl)
target_include_directories(trihexor PRIVATE "${imgui_RES_DIRS}/bindings")

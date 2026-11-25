cmake_minimum_required(VERSION 3.16)
project(road_warden CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(OpenGL_GL_PREFERENCE GLVND)
find_package(X11 REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS OpenGL GLX)
find_package(Threads REQUIRED)

add_executable(road_warden
  src/main.cpp
  src/platform.cpp
  src/gamepad.cpp
  src/audio.cpp
  src/render.cpp
  src/road.cpp
  src/game.cpp)

target_compile_options(road_warden PRIVATE -Wall -Wextra)
target_link_libraries(road_warden PRIVATE OpenGL::OpenGL OpenGL::GLX X11::X11 Threads::Threads ${CMAKE_DL_LIBS})

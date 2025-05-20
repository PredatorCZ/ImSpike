cmake_minimum_required(VERSION 3.3)

set(CMAKE_CXX_STANDARD 23)
project(ImSpike)
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib;${CMAKE_INSTALL_RPATH}")
#set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
#set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

set(BUILD_SHARED_LIBS
    ON
    CACHE BOOL "")
set(BUILD_STATIC_OBJECTS
    OFF
    CACHE BOOL "")
set(NO_OBJECTS
    ON
    CACHE BOOL "" FORCE)

add_subdirectory(precore)
include(targetex)
include(version)
toolset_version()

add_subdirectory(precore/gltf)
add_subdirectory(imgui)

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)
find_package(GLEW REQUIRED)

build_target(
  NAME
  imspike
  TYPE
  APP
  SOURCES
  src/main.cpp
  src/settings.cpp
  src/explorer.cpp
  src/logs.cpp
  src/modules.cpp
  src/batch.cpp
  src/ui_stack.cpp
  src/exec.cpp

  precore/spike/out_context.cpp
  precore/spike/in_context.cpp
  precore/spike/context.cpp
  precore/spike/out_cache.cpp
  precore/spike/in_cache.cpp
  precore/spike/tmp_storage.cpp
  precore/spike/batch.cpp

  INCLUDES
  precore/3rd_party/json
  DEFINITIONS
  MAIN_APP
  IMGUI_DEFINE_MATH_OPERATORS
  LINKS
  precore
  glfw
  OpenGL::GL
  GLEW::GLEW
  ImGui
  DESCR
  "Spike GUI"
  START_YEAR
  2023
)

install(
  TARGETS imspike
  RUNTIME DESTINATION ".")

install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION ".")

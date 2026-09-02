cmake_minimum_required(VERSION 3.20)
project(ember LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_executable(ember
  src/main.cpp
  src/cl_backend.cpp
  src/vk_interop.cpp
  src/scene.cpp
  src/bvh.cpp
  src/renderer.cpp
  src/image.cpp
)

target_include_directories(ember PRIVATE src)

if(MSVC)
  target_compile_options(ember PRIVATE /W4 /permissive-)
else()
  target_compile_options(ember PRIVATE -Wall -Wextra)
  target_link_libraries(ember PRIVATE ${CMAKE_DL_LIBS})
endif()

if(APPLE)
  target_compile_definitions(ember PRIVATE CL_SILENCE_DEPRECATION GL_SILENCE_DEPRECATION)
endif()

add_custom_command(TARGET ember POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/kernels $<TARGET_FILE_DIR:ember>/kernels)

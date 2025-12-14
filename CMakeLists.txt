cmake_minimum_required(VERSION 3.15)

project(PGR2025)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

option(SDL_SHARED "" OFF)
option(SDL_STATIC "" ON)
add_subdirectory(libs/SDL-release-3.2.24)
add_subdirectory(libs/geGL-master)


set(CMAKE_PREFIX_PATH "/opt/homebrew;$ENV{CMAKE_PREFIX_PATH}")
find_package(assimp REQUIRED)

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/bunny.cpp
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  SDL3::SDL3
  geGL::geGL
  assimp::assimp
  "-framework OpenGL"
)

add_custom_target(run ./${PROJECT_NAME})

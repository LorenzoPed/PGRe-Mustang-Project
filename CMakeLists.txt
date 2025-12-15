cmake_minimum_required(VERSION 3.15)

project(PGR2025)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

option(SDL_SHARED "" OFF)
option(SDL_STATIC "" ON)
add_subdirectory(libs/SDL-release-3.2.24)
add_subdirectory(libs/geGL-master)
add_subdirectory(libs/stb_image)
set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/glm-0.9.9.8)


set(CMAKE_PREFIX_PATH "/opt/homebrew;$ENV{CMAKE_PREFIX_PATH}")
find_package(assimp REQUIRED)

add_executable(${PROJECT_NAME}
  src/main.cpp
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  SDL3::SDL3
  geGL::geGL
  assimp::assimp
  "-framework OpenGL"
  glm::glm
  stb_image
)

add_custom_target(run ./${PROJECT_NAME})

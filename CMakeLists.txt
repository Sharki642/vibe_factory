cmake_minimum_required(VERSION 3.20)
project(factory LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # für clangd / VS Code

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- raylib: System-Installation nutzen, sonst automatisch von GitHub holen ---
set(RAYLIB_VERSION 6.2)
find_package(raylib ${RAYLIB_VERSION} QUIET)
if(NOT raylib_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG ${RAYLIB_VERSION}
    GIT_SHALLOW TRUE
  )
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(raylib)
endif()

# --- Spiel-Kern (ohne Fenster/Rendering, damit testbar) ---
add_library(factory_core STATIC src/world.cpp)
target_include_directories(factory_core PUBLIC src)
target_link_libraries(factory_core PUBLIC raylib) # nur wegen Color-Typ
target_compile_options(factory_core PRIVATE -Wall -Wextra)

# --- Spiel ---
add_executable(factory src/main.cpp src/render.cpp)
target_link_libraries(factory PRIVATE factory_core)
target_compile_options(factory PRIVATE -Wall -Wextra)

# --- Tests: ctest --test-dir build ---
enable_testing()
add_executable(world_test tests/world_test.cpp)
target_link_libraries(world_test PRIVATE factory_core)
add_test(NAME world_test COMMAND world_test)

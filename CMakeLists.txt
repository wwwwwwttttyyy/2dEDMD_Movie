# =============================================================
#  Build examples
#
#  Linux / macOS — Make:
#    mkdir build && cd build && cmake .. && make -j$(nproc)
#
#  Linux / macOS — Ninja (faster):
#    mkdir build && cd build && cmake .. -G Ninja && ninja
#
#  Force a specific compiler (before mkdir):
#    export CXX=g++   # or icpc
#
#  Windows — MinGW Make:
#    mkdir build && cd build
#    cmake .. -G "MinGW Makefiles" && mingw32-make -j4
#
#  Windows — Ninja:
#    mkdir build && cd build
#    cmake .. -G Ninja && ninja
# =============================================================

cmake_minimum_required(VERSION 3.10)

# On Linux, prefer ICC if available (must be set before project())
if(UNIX AND NOT APPLE)
    find_program(ICC_CXX icpc)
    if(ICC_CXX)
        set(CMAKE_CXX_COMPILER "${ICC_CXX}" CACHE FILEPATH "C++ compiler" FORCE)
        message(STATUS "Using Intel C++ Compiler: ${ICC_CXX}")
    endif()
endif()

project(edmd2d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(edmd2d main.cpp src/edmd2d.cpp)
target_include_directories(edmd2d PRIVATE include)

if(MSVC)
    target_compile_options(edmd2d PRIVATE /O2 /W3)
else()
    target_compile_options(edmd2d PRIVATE -O3 -march=native -Wall)
    target_link_libraries(edmd2d PRIVATE m)
endif()

# =============================================================
#  OpenGL 可视化前端（基于 raylib，通过 FetchContent 自动下载）
#
#  构建两个目标：
#    viewer        — 文件回放模式（仅需 raylib）
#    viewer_sim    — 实时仿真模式（raylib + edmd2d 仿真逻辑）
#
#  如需跳过联网下载，可设置：  cmake .. -DBUILD_VIEWER=OFF
# =============================================================
option(BUILD_VIEWER "Build the OpenGL viewer (requires internet on first build)" ON)

if(BUILD_VIEWER)
    include(FetchContent)

    # 关闭 raylib 自带的示例与游戏，加速构建
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
    set(OPENGL_VERSION "3.3" CACHE STRING "" FORCE)

    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG        5.5
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(raylib)

    # ---- 文件回放 viewer ----
    add_executable(viewer viewer.cpp)
    target_include_directories(viewer PRIVATE include ${raylib_SOURCE_DIR}/src)
    target_link_libraries(viewer PRIVATE raylib)

    if(MSVC)
        target_compile_options(viewer PRIVATE /O2 /W3)
    else()
        target_compile_options(viewer PRIVATE -O3 -Wall)
    endif()

    # ---- 实时仿真 viewer_sim（链接 edmd2d 仿真库）----
    add_executable(viewer_sim viewer.cpp src/edmd2d.cpp)
    target_include_directories(viewer_sim PRIVATE include ${raylib_SOURCE_DIR}/src)
    target_compile_definitions(viewer_sim PRIVATE BUILD_WITH_EDMD)
    target_link_libraries(viewer_sim PRIVATE raylib)

    if(MSVC)
        target_compile_options(viewer_sim PRIVATE /O2 /W3)
    else()
        target_compile_options(viewer_sim PRIVATE -O3 -Wall)
        target_link_libraries(viewer_sim PRIVATE m)
    endif()

    message(STATUS "OpenGL Viewer targets: viewer (文件回放), viewer_sim (实时仿真)")
endif()

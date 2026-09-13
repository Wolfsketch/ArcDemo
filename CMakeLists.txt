cmake_minimum_required(VERSION 3.20)

project(ArcDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ============================================================
# TinyGLTF
# Voor .glb / .gltf bestanden.
# Geen game engine: alleen een file parser.
# ============================================================

set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.5
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(tinygltf)

# ============================================================
# TinyObjLoader
# Voor Wavefront .obj bestanden.
# ============================================================

FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v2.0.0rc13
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(tinyobjloader)

# ============================================================
# ArcDemo executable
# ============================================================

add_executable(
    ArcDemo
    WIN32

    src/main.cpp

    src/Game.cpp
    src/Renderer.cpp
    src/Camera.cpp

    src/Model.cpp
    src/AnimatedModel.cpp
    src/TestCharacter.cpp
    src/ObjModel.cpp

    src/Terrain.cpp
    src/Scene.cpp

    src/Enemy.cpp
    src/Weapon.cpp
)

# ============================================================
# Include directories
# ============================================================

target_include_directories(
    ArcDemo
    PRIVATE

    ${CMAKE_SOURCE_DIR}/src

    ${tinygltf_SOURCE_DIR}
    ${tinyobjloader_SOURCE_DIR}
)

# ============================================================
# Windows / Unicode
# ============================================================

target_compile_definitions(
    ArcDemo
    PRIVATE

    UNICODE
    _UNICODE
)

# ============================================================
# DirectX / Windows libraries
# ============================================================

target_link_libraries(
    ArcDemo
    PRIVATE

    user32

    d3d11
    dxgi
    d3dcompiler
)

# ============================================================
# Copy assets next to ArcDemo.exe
#
# build/Debug/
# ├─ ArcDemo.exe
# └─ assets/
#    └─ models/
# ============================================================

add_custom_command(
    TARGET ArcDemo
    POST_BUILD

    COMMAND
        ${CMAKE_COMMAND}
        -E
        copy_directory

        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:ArcDemo>/assets"
)
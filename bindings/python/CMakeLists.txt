cmake_minimum_required(VERSION 3.18)

project(engram_python LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(pybind11 CONFIG REQUIRED)

# Bind against the single-header build so no separate engram.cpp is needed.
pybind11_add_module(_engram engram_ext.cpp)
target_include_directories(_engram PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../single_header)
target_compile_features(_engram PRIVATE cxx_std_20)

# Optional: drop the per-allocation count/total counters (Arena.count / Arena.total
# then always read 0).
option(ENGRAM_DISABLE_TRACKING "Disable arena count/total allocation tracking." OFF)
if(ENGRAM_DISABLE_TRACKING)
    target_compile_definitions(_engram PRIVATE ENGRAM_DISABLE_TRACKING)
endif()

# Optional: drop save/restore. Arena.save / restore / save_depth disappear with it.
option(ENGRAM_DISABLE_SAVE_RESTORE "Drop save/restore and the save-point stack." OFF)
if(ENGRAM_DISABLE_SAVE_RESTORE)
    target_compile_definitions(_engram PRIVATE ENGRAM_DISABLE_SAVE_RESTORE)
endif()

# Optional: add the untyped Arena.pop() and Arena.push_depth.
option(ENGRAM_EASY_POP "Record every push so an untyped pop() can rewind it." OFF)
if(ENGRAM_EASY_POP)
    target_compile_definitions(_engram PRIVATE ENGRAM_EASY_POP)
endif()

# Optional: record where each arena was created, exposed as Arena.origin. The site
# recorded is inside the bindings, not the calling Python frame.
option(ENGRAM_ENABLE_SOURCE_INFO "Record the creation site of every arena." OFF)
if(ENGRAM_ENABLE_SOURCE_INFO)
    target_compile_definitions(_engram PRIVATE ENGRAM_ENABLE_SOURCE_INFO)
endif()

# The single header auto-enables the platform's default device backend
# (DirectX 12 on Windows, Metal on Apple); link the corresponding system
# libraries. No third-party SDK is required.
if(WIN32)
    target_link_libraries(_engram PRIVATE d3d12 dxgi d3dcompiler)
elseif(APPLE)
    target_link_libraries(_engram PRIVATE
        "-framework Metal" "-framework Foundation" "-framework QuartzCore" objc)
endif()

install(TARGETS _engram DESTINATION .)
install(FILES engram.py DESTINATION .)

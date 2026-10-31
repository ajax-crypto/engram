cmake_minimum_required(VERSION 3.18)

project(engram_python LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(pybind11 CONFIG REQUIRED)

# Bind against the header + source build: single_header/engram.h is
# freestanding-only (stack/external arenas, no heap, no vendor backends), so the
# bindings need the full-featured split build instead.
pybind11_add_module(_engram engram_ext.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../src/engram.cpp)
target_include_directories(_engram PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
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

# src/engram.h auto-enables the platform's default device backend on Windows
# (DirectX 12); Metal needs an explicit define there since it has no auto-enable
# or CMake option of its own. Link the corresponding system libraries — no
# third-party SDK is required for either.
if(WIN32)
    # The heap backend reaches for VirtualAlloc2 and the token APIs (onecore / advapi32).
    target_link_libraries(_engram PRIVATE d3d12 dxgi d3dcompiler advapi32 onecore)
elseif(APPLE)
    target_compile_definitions(_engram PRIVATE ENGRAM_ENABLE_METAL)
    target_link_libraries(_engram PRIVATE
        "-framework Metal" "-framework Foundation" "-framework QuartzCore" objc)
endif()

install(TARGETS _engram DESTINATION .)
install(FILES engram.py DESTINATION .)

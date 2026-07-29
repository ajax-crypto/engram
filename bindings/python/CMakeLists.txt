cmake_minimum_required(VERSION 3.18)

project(engram_python LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(pybind11 CONFIG REQUIRED)

# Bind against the single-header build so no separate engram.cpp is needed.
pybind11_add_module(_engram engram_ext.cpp)
target_include_directories(_engram PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../single_header)
target_compile_features(_engram PRIVATE cxx_std_20)

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

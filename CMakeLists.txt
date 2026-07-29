cmake_minimum_required(VERSION 3.20)

project(engram
    VERSION 0.1.0
    DESCRIPTION "C++20 arena allocator with pluggable memory backends"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Build-shape options
# ---------------------------------------------------------------------------
# ON  -> header-only build using single_header/engram.h (INTERFACE library).
# OFF -> split build using src/engram.h + src/engram.cpp (compiled library).
option(ENGRAM_SINGLE_HEADER
    "Use the single-header build. When OFF, compiles the src/ header+cpp version." ON)

# ---------------------------------------------------------------------------
# Feature options
# ---------------------------------------------------------------------------
option(ENGRAM_ALL         "Enable every backend supported on the current platform." OFF)
option(ENGRAM_DISABLE_PMR "Disable std::pmr integration."                           OFF)
option(ENGRAM_EASY_POP    "Enable the fixed-size array pop helpers."                 OFF)

# ---------------------------------------------------------------------------
# Per-backend options (map 1:1 to ENGRAM_ENABLE_* preprocessor defines)
# ---------------------------------------------------------------------------
option(ENGRAM_ENABLE_VULKAN     "Enable the Vulkan backend."                         OFF)
option(ENGRAM_ENABLE_DX12       "Enable the DirectX 12 backend (Windows)."           OFF)
option(ENGRAM_ENABLE_CUDA       "Enable the CUDA backend."                           OFF)
option(ENGRAM_ENABLE_ROCM       "Enable the ROCm/HIP backend."                       OFF)
option(ENGRAM_ENABLE_XDNA       "Enable the XDNA/XRT backend."                       OFF)
option(ENGRAM_ENABLE_OPENCL     "Enable the OpenCL backend."                         OFF)
option(ENGRAM_ENABLE_SYCL       "Enable the SYCL backend."                           OFF)
option(ENGRAM_ENABLE_LEVEL_ZERO "Enable the Level Zero backend."                     OFF)
option(ENGRAM_ENABLE_WEBGPU     "Enable the WebGPU backend."                         OFF)
option(ENGRAM_ENABLE_PMDK       "Enable the PMDK persistent-memory backend."         OFF)
option(ENGRAM_ENABLE_DPDK       "Enable the DPDK backend (Linux only)."              OFF)
option(ENGRAM_ENABLE_RDMA       "Enable the RDMA/ibverbs backend (Linux only)."      OFF)
option(ENGRAM_ENABLE_GPUDIRECT  "Enable the GPUDirect Storage backend (Linux only)." OFF)

# ---------------------------------------------------------------------------
# Collect the resulting preprocessor definitions
# ---------------------------------------------------------------------------
set(ENGRAM_DEFINITIONS "")

if(ENGRAM_ALL)
    list(APPEND ENGRAM_DEFINITIONS ENGRAM_ALL)
endif()
if(ENGRAM_DISABLE_PMR)
    list(APPEND ENGRAM_DEFINITIONS ENGRAM_DISABLE_PMR)
endif()
if(ENGRAM_EASY_POP)
    list(APPEND ENGRAM_DEFINITIONS ENGRAM_EASY_POP)
endif()

foreach(backend
        VULKAN DX12 CUDA ROCM XDNA OPENCL SYCL
        LEVEL_ZERO WEBGPU PMDK DPDK RDMA GPUDIRECT)
    if(ENGRAM_ENABLE_${backend})
        list(APPEND ENGRAM_DEFINITIONS ENGRAM_ENABLE_${backend})
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
if(ENGRAM_SINGLE_HEADER)
    add_library(engram INTERFACE)
    set(ENGRAM_SCOPE INTERFACE)
    target_include_directories(engram INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/single_header>
        $<INSTALL_INTERFACE:include>)
else()
    add_library(engram STATIC src/engram.cpp)
    set(ENGRAM_SCOPE PUBLIC)
    target_include_directories(engram PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>)
endif()

add_library(engram::engram ALIAS engram)

target_compile_features(engram ${ENGRAM_SCOPE} cxx_std_20)
target_compile_definitions(engram ${ENGRAM_SCOPE} ${ENGRAM_DEFINITIONS})

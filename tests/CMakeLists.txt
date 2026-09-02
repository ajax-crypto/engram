# CppUTest-based test suite. Each target builds engram in a different shape, so
# the sources are compiled directly here rather than through the `engram` target
# (which follows ENGRAM_SINGLE_HEADER).

find_package(CppUTest QUIET)

if(NOT TARGET CppUTest AND NOT TARGET CppUTest::CppUTest)
    include(FetchContent)

    set(TESTS OFF CACHE BOOL "" FORCE)       # don't build CppUTest's own tests
    set(EXTENSIONS ON CACHE BOOL "" FORCE)   # CppUTestExt provides the mocking support

    # CppUTest 4.0 still asks for cmake_minimum_required(VERSION 3.1), which
    # CMake 4 rejects outright.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

    FetchContent_Declare(CppUTest
        GIT_REPOSITORY https://github.com/cpputest/cpputest.git
        GIT_TAG        v4.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(CppUTest)
endif()

if(TARGET CppUTest::CppUTest)
    set(ENGRAM_CPPUTEST CppUTest::CppUTest CppUTest::CppUTestExt)
else()
    set(ENGRAM_CPPUTEST CppUTest CppUTestExt)
endif()

function(engram_add_test name)
    cmake_parse_arguments(ARG "" "STANDARD" "SOURCES;INCLUDES;DEFINES;LIBS" ${ARGN})

    if(NOT ARG_STANDARD)
        set(ARG_STANDARD cxx_std_20)
    endif()

    add_executable(${name} ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE ${ARG_INCLUDES})
    target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
    target_compile_features(${name} PRIVATE ${ARG_STANDARD})
    target_link_libraries(${name} PRIVATE ${ENGRAM_CPPUTEST} ${ARG_LIBS})

    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /EHsc)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

# The heap backend reaches for VirtualAlloc2 and the token APIs on Windows.
set(ENGRAM_HOSTED_LIBS "")
if(WIN32)
    set(ENGRAM_HOSTED_LIBS advapi32 onecore)
endif()

engram_add_test(engram_test_hcpp
    SOURCES  test_hcpp.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/engram.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    DEFINES  ${ENGRAM_DEFINITIONS}
    LIBS     ${ENGRAM_HOSTED_LIBS})

engram_add_test(engram_test_single_h
    SOURCES  test_single_h.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
    DEFINES  ${ENGRAM_DEFINITIONS}
    LIBS     ${ENGRAM_HOSTED_LIBS})

# Same suites again with the exception-masking policy selected.
engram_add_test(engram_test_hcpp_masked
    SOURCES  test_hcpp.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/engram.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    DEFINES  ${ENGRAM_DEFINITIONS} ENGRAM_MASK_EXCEPTIONS
    LIBS     ${ENGRAM_HOSTED_LIBS})

engram_add_test(engram_test_single_h_masked
    SOURCES  test_single_h.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
    DEFINES  ${ENGRAM_DEFINITIONS} ENGRAM_MASK_EXCEPTIONS
    LIBS     ${ENGRAM_HOSTED_LIBS})

# And again with arena creation sites recorded.
engram_add_test(engram_test_hcpp_srcinfo
    SOURCES  test_hcpp.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/engram.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    DEFINES  ${ENGRAM_DEFINITIONS} ENGRAM_ENABLE_SOURCE_INFO
    LIBS     ${ENGRAM_HOSTED_LIBS})

engram_add_test(engram_test_single_h_srcinfo
    SOURCES  test_single_h.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
    DEFINES  ${ENGRAM_DEFINITIONS} ENGRAM_ENABLE_SOURCE_INFO
    LIBS     ${ENGRAM_HOSTED_LIBS})

# And again with the untyped pop() enabled.
engram_add_test(engram_test_hcpp_easypop
    SOURCES  test_hcpp.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/engram.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    DEFINES  ${ENGRAM_DEFINITIONS} ENGRAM_EASY_POP
    LIBS     ${ENGRAM_HOSTED_LIBS})

engram_add_test(engram_test_single_h_easypop
    SOURCES  test_single_h.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
    DEFINES  ${ENGRAM_DEFINITIONS} ENGRAM_EASY_POP
    LIBS     ${ENGRAM_HOSTED_LIBS})

# Trimmed profiles: what is left once save/restore, and then everything optional,
# is compiled out.
engram_add_test(engram_test_hcpp_nosave
    SOURCES  test_profiles.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/engram.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    DEFINES  ${ENGRAM_DEFINITIONS} ENGRAM_DISABLE_SAVE_RESTORE
    LIBS     ${ENGRAM_HOSTED_LIBS})

engram_add_test(engram_test_single_h_nosave
    SOURCES  test_profiles.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
    DEFINES  ${ENGRAM_DEFINITIONS} ENGRAM_DISABLE_SAVE_RESTORE
    LIBS     ${ENGRAM_HOSTED_LIBS})

engram_add_test(engram_test_hcpp_minimal
    SOURCES  test_profiles.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/engram.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    DEFINES  ENGRAM_MINIMAL
    LIBS     ${ENGRAM_HOSTED_LIBS})

engram_add_test(engram_test_single_h_minimal
    SOURCES  test_profiles.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
    DEFINES  ENGRAM_MINIMAL
    LIBS     ${ENGRAM_HOSTED_LIBS})

engram_add_test(engram_test_freestanding
    SOURCES  test_freestanding.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
    DEFINES  ENGRAM_ENABLE_FREESTANDING)

engram_add_test(engram_test_freestanding_fsextra
    SOURCES  test_freestanding.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
    DEFINES  ENGRAM_ENABLE_FREESTANDING ENGRAM_ENABLE_FSEXTRA)

# push_md_array switches itself on when the standard library has <mdspan>, so the
# only thing needed to cover it is a C++23 build of the same suites.
include(CheckIncludeFileCXX)
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX23_STANDARD_COMPILE_OPTION}")
check_include_file_cxx(mdspan ENGRAM_HAVE_MDSPAN)
unset(CMAKE_REQUIRED_FLAGS)

if(ENGRAM_HAVE_MDSPAN)
    engram_add_test(engram_test_hcpp_cxx23
        SOURCES  test_hcpp.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/engram.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
        DEFINES  ${ENGRAM_DEFINITIONS}
        STANDARD cxx_std_23
        LIBS     ${ENGRAM_HOSTED_LIBS})

    engram_add_test(engram_test_single_h_cxx23
        SOURCES  test_single_h.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
        DEFINES  ${ENGRAM_DEFINITIONS}
        STANDARD cxx_std_23
        LIBS     ${ENGRAM_HOSTED_LIBS})
else()
    message(STATUS "engram: <mdspan> not available; skipping the push_md_array tests")
endif()

# ---------------------------------------------------------------------------
# Linux vendor backends: compile-only conformance against the real SDK headers.
#
# These backends need hardware, a kernel module, or root to run, and their driver
# libraries are not linked here. What is under test is that engram's backend code
# still type-checks against the vendor headers, so each target is an OBJECT library
# and a mismatch shows up as a build failure.
#
# Headers already on the system win; otherwise the header-only SDKs are fetched.
# Anything that cannot be resolved is skipped with a message rather than failing.
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    option(ENGRAM_FETCH_VENDOR_HEADERS
        "Download the header-only vendor SDKs the Linux conformance targets need." ON)

    # Pinned so a fetch is reproducible; override if a tag moves or is unavailable.
    set(ENGRAM_VULKAN_HEADERS_TAG "vulkan-sdk-1.3.290.0" CACHE STRING "Vulkan-Headers git tag.")
    set(ENGRAM_WEBGPU_HEADERS_TAG "main"                 CACHE STRING "webgpu-headers git tag.")
    set(ENGRAM_PMDK_TAG           "1.13.1"               CACHE STRING "PMDK git tag.")

    include(FetchContent)

    set(ENGRAM_VENDOR_BACKENDS "")
    set(ENGRAM_VENDOR_SKIPPED "")

    # Populate without configuring: pointing SOURCE_SUBDIR at a directory that does
    # not exist stops MakeAvailable from calling add_subdirectory, which is what we
    # want for header-only SDKs (and FetchContent_Populate is deprecated).
    function(engram_fetch_headers_only name repo tag out_dir)
        FetchContent_Declare(${name}
            GIT_REPOSITORY ${repo}
            GIT_TAG        ${tag}
            GIT_SHALLOW    TRUE
            SOURCE_SUBDIR  engram-no-configure)
        FetchContent_MakeAvailable(${name})
        set(${out_dir} ${${name}_SOURCE_DIR} PARENT_SCOPE)
    endfunction()

    # --- Vulkan: header-only upstream, so a fetch is reliable -----------------
    find_path(ENGRAM_VULKAN_INCLUDE_DIR vulkan/vulkan.h)
    if(ENGRAM_VULKAN_INCLUDE_DIR)
        set(ENGRAM_VENDOR_VULKAN_INCLUDES ${ENGRAM_VULKAN_INCLUDE_DIR})
        list(APPEND ENGRAM_VENDOR_BACKENDS VULKAN)
    elseif(ENGRAM_FETCH_VENDOR_HEADERS)
        engram_fetch_headers_only(vulkan_headers
            https://github.com/KhronosGroup/Vulkan-Headers.git
            ${ENGRAM_VULKAN_HEADERS_TAG} vulkan_src)
        set(ENGRAM_VENDOR_VULKAN_INCLUDES ${vulkan_src}/include)
        list(APPEND ENGRAM_VENDOR_BACKENDS VULKAN)
    else()
        list(APPEND ENGRAM_VENDOR_SKIPPED "Vulkan")
    endif()

    # --- WebGPU: upstream ships webgpu.h at the repo root while engram includes
    #     <webgpu/webgpu.h>, so stage it into the expected layout --------------
    find_path(ENGRAM_WEBGPU_INCLUDE_DIR webgpu/webgpu.h)
    if(ENGRAM_WEBGPU_INCLUDE_DIR)
        set(ENGRAM_VENDOR_WEBGPU_INCLUDES ${ENGRAM_WEBGPU_INCLUDE_DIR})
        list(APPEND ENGRAM_VENDOR_BACKENDS WEBGPU)
    elseif(ENGRAM_FETCH_VENDOR_HEADERS)
        engram_fetch_headers_only(webgpu_headers
            https://github.com/webgpu-native/webgpu-headers.git
            ${ENGRAM_WEBGPU_HEADERS_TAG} webgpu_src)
        file(COPY ${webgpu_src}/webgpu.h
             DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/vendor_include/webgpu)
        set(ENGRAM_VENDOR_WEBGPU_INCLUDES ${CMAKE_CURRENT_BINARY_DIR}/vendor_include)
        list(APPEND ENGRAM_VENDOR_BACKENDS WEBGPU)
    else()
        list(APPEND ENGRAM_VENDOR_SKIPPED "WebGPU")
    endif()

    # --- PMDK: libpmem.h is self-contained in the source tree ----------------
    find_path(ENGRAM_PMDK_INCLUDE_DIR libpmem.h)
    if(ENGRAM_PMDK_INCLUDE_DIR)
        set(ENGRAM_VENDOR_PMDK_INCLUDES ${ENGRAM_PMDK_INCLUDE_DIR})
        list(APPEND ENGRAM_VENDOR_BACKENDS PMDK)
    elseif(ENGRAM_FETCH_VENDOR_HEADERS)
        engram_fetch_headers_only(pmdk
            https://github.com/pmem/pmdk.git
            ${ENGRAM_PMDK_TAG} pmdk_src)
        set(ENGRAM_VENDOR_PMDK_INCLUDES ${pmdk_src}/src/include)
        list(APPEND ENGRAM_VENDOR_BACKENDS PMDK)
    else()
        list(APPEND ENGRAM_VENDOR_SKIPPED "PMDK")
    endif()

    # --- RDMA: rdma-core generates part of infiniband/verbs_api.h at build time,
    #     so rebuilding the include tree from a bare checkout is fragile. Take the
    #     distro package when it is present -----------------------------------
    find_path(ENGRAM_RDMA_INCLUDE_DIR infiniband/verbs.h)
    if(ENGRAM_RDMA_INCLUDE_DIR)
        set(ENGRAM_VENDOR_RDMA_INCLUDES ${ENGRAM_RDMA_INCLUDE_DIR})
        list(APPEND ENGRAM_VENDOR_BACKENDS RDMA)
    else()
        list(APPEND ENGRAM_VENDOR_SKIPPED "RDMA (apt install libibverbs-dev)")
    endif()

    # --- dma-buf: a kernel UAPI header, so it comes from linux-libc-dev ------
    find_path(ENGRAM_DMABUF_INCLUDE_DIR linux/dma-heap.h)
    if(ENGRAM_DMABUF_INCLUDE_DIR)
        set(ENGRAM_VENDOR_DMABUF_INCLUDES ${ENGRAM_DMABUF_INCLUDE_DIR})
        list(APPEND ENGRAM_VENDOR_BACKENDS DMABUF)
    else()
        list(APPEND ENGRAM_VENDOR_SKIPPED "dma-buf (needs linux/dma-heap.h)")
    endif()

    # Compiled, never linked: the driver symbols stay deliberately unresolved.
    function(engram_add_vendor_conformance name)
        cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES;DEFINES" ${ARGN})

        add_library(${name} OBJECT ${ARG_SOURCES})
        target_include_directories(${name} PRIVATE ${ARG_INCLUDES})
        target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
        target_compile_features(${name} PRIVATE cxx_std_20)
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endfunction()

    foreach(backend IN LISTS ENGRAM_VENDOR_BACKENDS)
        string(TOLOWER ${backend} lower)

        engram_add_vendor_conformance(engram_vendor_single_h_${lower}
            SOURCES  test_vendor_linux.cpp
            INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
                     ${ENGRAM_VENDOR_${backend}_INCLUDES}
            DEFINES  ENGRAM_ENABLE_${backend} ENGRAM_SINGLE_HEADER_BUILD)

        # The split build keeps its vendor code in engram.cpp, so compile that
        # against the same headers rather than only the calling side.
        engram_add_vendor_conformance(engram_vendor_hcpp_${lower}
            SOURCES  test_vendor_linux.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/engram.cpp
            INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
                     ${ENGRAM_VENDOR_${backend}_INCLUDES}
            DEFINES  ENGRAM_ENABLE_${backend})
    endforeach()

    if(ENGRAM_VENDOR_BACKENDS)
        string(REPLACE ";" ", " ENGRAM_VENDOR_LIST "${ENGRAM_VENDOR_BACKENDS}")
        message(STATUS "engram: vendor conformance (compile-only) for ${ENGRAM_VENDOR_LIST}")
    endif()
    if(ENGRAM_VENDOR_SKIPPED)
        string(REPLACE ";" ", " ENGRAM_VENDOR_SKIP_LIST "${ENGRAM_VENDOR_SKIPPED}")
        message(STATUS "engram: vendor conformance skipped for ${ENGRAM_VENDOR_SKIP_LIST}")
    endif()
endif()

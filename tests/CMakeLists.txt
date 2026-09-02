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

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
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES;DEFINES;LIBS" ${ARGN})

    add_executable(${name} ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE ${ARG_INCLUDES})
    target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
    target_compile_features(${name} PRIVATE cxx_std_20)
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

engram_add_test(engram_test_freestanding
    SOURCES  test_freestanding.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
    DEFINES  ENGRAM_ENABLE_FREESTANDING)

engram_add_test(engram_test_freestanding_fsextra
    SOURCES  test_freestanding.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../single_header
    DEFINES  ENGRAM_ENABLE_FREESTANDING ENGRAM_ENABLE_FSEXTRA)

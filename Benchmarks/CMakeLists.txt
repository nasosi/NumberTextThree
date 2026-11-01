cmake_minimum_required( VERSION 3.25 )

# Default to Release unless a build type was chosen. This has to come before project(), because project() would
# otherwise pick Debug for MSVC.
if ( NOT CMAKE_BUILD_TYPE AND NOT DEFINED ENV{CMAKE_BUILD_TYPE} )
    set( CMAKE_BUILD_TYPE Release CACHE STRING "Build type" )
endif()

# This is a standalone project, configured with cmake -S Benchmarks -B out/bench. The library's own CMakeLists.txt
# never sees it, which means library users never pull in these dependencies.
project( NumberTextThreeBenchmarks LANGUAGES C CXX )

get_property( multiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG )

if ( NOT multiConfig AND NOT CMAKE_BUILD_TYPE STREQUAL "Release" )
    message( WARNING "This is a ${CMAKE_BUILD_TYPE} build; the timings mean something only in a Release build." )
endif()

# Every library, first party and third party alike, is built the same way, with C++20, -O2 or /O2, and NDEBUG.
set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

if ( MSVC )
    set( CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG" )
    set( CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG" )
else()
    set( CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" )
    set( CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" )
endif()

# The competing libraries, each pinned to a full commit hash. SYSTEM keeps their headers out of the warnings we turn
# on for our own code.
include( FetchContent )

# v8.3.2
FetchContent_Declare( fast_float
    GIT_REPOSITORY https://github.com/fastfloat/fast_float.git
    GIT_TAG        42f254836ee416ffcf2f4e6576260003ac33d48c
    SYSTEM )

# master
FetchContent_Declare( ryu
    GIT_REPOSITORY https://github.com/ulfjack/ryu.git
    GIT_TAG        4c0618b0e44f7ef027ebae05d2cc7812048f7c8f
    SYSTEM )

# master
FetchContent_Declare( dragonbox
    GIT_REPOSITORY https://github.com/jk-jeon/dragonbox.git
    GIT_TAG        beeeef91cf6fef89a4d4ba5e95d47ca64ccb3a44
    SYSTEM )

# main
FetchContent_Declare( zmij
    GIT_REPOSITORY https://github.com/vitaut/zmij.git
    GIT_TAG        55ddf6aaccf922bfac289dece53d7ecc0540e314
    SYSTEM )

# main
FetchContent_Declare( xjb
    GIT_REPOSITORY https://github.com/xjb714/xjb.git
    GIT_TAG        80cc89574a8f8457ffbf951afa2fd27c2459bd4a
    SYSTEM )

set( FASTFLOAT_INSTALL OFF )
set( ZMIJ_TEST OFF )
set( ZMIJ_EXAMPLE OFF )

FetchContent_MakeAvailable( fast_float ryu dragonbox zmij xjb )

# xjb's own target builds src/ftoa.cpp but does not publish an include directory, so we add one here.
target_include_directories( xjb SYSTEM INTERFACE ${xjb_SOURCE_DIR}/src )

# canada.json comes from nativejson-benchmark, and it is the usual real-world set of coordinates.
set( canadaUrl https://raw.githubusercontent.com/miloyip/nativejson-benchmark/478d5727c2a4048e835a29c65adecc7d795360d5/data/canada.json )
set( canadaHash f83b3b354030d5dd58740c68ac4fecef64cb730a0d12a90362a7f23077f50d78 )
set( canadaPath ${CMAKE_CURRENT_BINARY_DIR}/data/canada.json )

if ( EXISTS ${canadaPath} )
    file( SHA256 ${canadaPath} canadaFoundHash )
endif()

if ( NOT canadaFoundHash STREQUAL canadaHash )
    file( DOWNLOAD ${canadaUrl} ${canadaPath} EXPECTED_HASH SHA256=${canadaHash} TLS_VERIFY ON )
endif()

add_executable( Bench Bench.cpp Datasets.cpp )
target_include_directories( Bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. )
target_link_libraries( Bench PRIVATE FastFloat::fast_float ryu::ryu dragonbox::dragonbox_to_chars zmij::zmij xjb )

if ( MSVC )
    target_compile_options( Bench PRIVATE /W4 /WX )
    target_compile_definitions( Bench PRIVATE _CRT_SECURE_NO_WARNINGS )
else()
    target_compile_options( Bench PRIVATE -Wall -Wextra -Wpedantic -Werror )
endif()

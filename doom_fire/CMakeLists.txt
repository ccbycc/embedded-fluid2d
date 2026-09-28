cmake_minimum_required(VERSION 3.16)
project(doom_fire LANGUAGES CXX)

add_library(doom_fire STATIC
    doom_fire.cpp
)

target_include_directories(doom_fire PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(doom_fire PUBLIC cxx_std_11)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DOOM_FIRE_BUILD_TESTS_DEFAULT ON)
else()
    set(DOOM_FIRE_BUILD_TESTS_DEFAULT OFF)
endif()
option(DOOM_FIRE_BUILD_TESTS "Build doom_fire host tests"
       ${DOOM_FIRE_BUILD_TESTS_DEFAULT})
if(DOOM_FIRE_BUILD_TESTS)
    enable_testing()
    add_executable(doom_fire_smoke_test smoke_test.cpp)
    target_link_libraries(doom_fire_smoke_test PRIVATE doom_fire)
    add_test(NAME doom_fire_smoke_test COMMAND doom_fire_smoke_test)
endif()

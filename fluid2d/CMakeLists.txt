cmake_minimum_required(VERSION 3.16)
project(fluid2d LANGUAGES CXX)

add_library(fluid2d STATIC
    fluid_solver.cpp
)

target_include_directories(fluid2d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(fluid2d PUBLIC cxx_std_11)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(FLUID2D_BUILD_TESTS_DEFAULT ON)
else()
    set(FLUID2D_BUILD_TESTS_DEFAULT OFF)
endif()
option(FLUID2D_BUILD_TESTS "Build fluid2d host tests"
       ${FLUID2D_BUILD_TESTS_DEFAULT})
if(FLUID2D_BUILD_TESTS)
    enable_testing()
    add_executable(fluid2d_smoke_test smoke_test.cpp)
    target_link_libraries(fluid2d_smoke_test PRIVATE fluid2d)
    add_test(NAME fluid2d_smoke_test COMMAND fluid2d_smoke_test)
endif()

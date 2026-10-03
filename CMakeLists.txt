cmake_minimum_required(VERSION 3.21)
project(FinRenamer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FINRENAMER_BUILD_TESTS "Build the unit tests" ON)

add_subdirectory(core)

# The Qt GUI goes here in a later step:
# add_subdirectory(app)

if(FINRENAMER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

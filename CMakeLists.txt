cmake_minimum_required(VERSION 3.21)
project(FinRenamer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FINRENAMER_BUILD_APP "Build the Qt GUI (needs Qt 6)" ON)
option(FINRENAMER_BUILD_TESTS "Build the unit tests" ON)

add_subdirectory(core)

if(FINRENAMER_BUILD_APP)
    add_subdirectory(app)
endif()

if(FINRENAMER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

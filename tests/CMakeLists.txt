include(FetchContent)

find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(finrenamer_tests
    TestHelpers.h
    DateSpecTests.cpp
    FilenameBuilderTests.cpp
    FolderPlannerTests.cpp
    RenamePlanTests.cpp
    RenameEngineTests.cpp
    DatabaseTests.cpp
)
target_link_libraries(finrenamer_tests PRIVATE finrenamer_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(finrenamer_tests)

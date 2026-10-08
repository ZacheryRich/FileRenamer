include(FetchContent)

# SQLiteCpp: use vcpkg's copy if installed (vcpkg install sqlitecpp),
# otherwise download it. The download bundles SQLite itself.
find_package(SQLiteCpp CONFIG QUIET)
if(NOT SQLiteCpp_FOUND)
    set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
    set(SQLITECPP_RUN_CPPCHECK OFF CACHE BOOL "" FORCE)
    set(SQLITECPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SQLITECPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SQLITECPP_INTERNAL_SQLITE ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SQLiteCpp
        GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
        GIT_TAG 3.3.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SQLiteCpp)
endif()

add_library(finrenamer_core STATIC
    src/Database.cpp
    src/DeficiencyReport.cpp
    src/DocxWriter.cpp
    src/DateSpec.cpp
    src/FilenameBuilder.cpp
    src/FolderPlanner.cpp
    src/FileScanner.cpp
    src/FolderSearch.cpp
    src/NameFixer.cpp
    src/RenamePlan.cpp
    src/RenameSession.cpp
    src/RenameEngine.cpp
    src/StatementCoverage.cpp
    src/StatementNames.cpp
    src/LooseStatementNames.cpp
    src/Utf8Path.cpp
)

target_include_directories(finrenamer_core PUBLIC include)

# PRIVATE: SQLiteCpp never appears in our headers, so the GUI doesn't see it.
target_link_libraries(finrenamer_core PRIVATE SQLiteCpp)

if(MSVC)
    # /utf-8: source and execution charset are UTF-8 (names like "José" stay intact).
    target_compile_options(finrenamer_core PUBLIC /utf-8 /permissive- /W4)
else()
    target_compile_options(finrenamer_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

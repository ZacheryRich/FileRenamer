add_library(finrenamer_core STATIC
    src/DateSpec.cpp
    src/FilenameBuilder.cpp
    src/FolderPlanner.cpp
    src/FileScanner.cpp
    src/RenamePlan.cpp
    src/RenameEngine.cpp
    src/Utf8Path.cpp
)

target_include_directories(finrenamer_core PUBLIC include)

if(MSVC)
    # /utf-8: source and execution charset are UTF-8 (names like "José" stay intact).
    target_compile_options(finrenamer_core PUBLIC /utf-8 /permissive- /W4)
else()
    target_compile_options(finrenamer_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

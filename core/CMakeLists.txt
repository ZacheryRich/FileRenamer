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

# OpenXLSX reads and writes .xlsx files (Bates log tools). Same approach: use an
# installed copy (vcpkg install openxlsx) if there is one, else download it.
find_package(OpenXLSX CONFIG QUIET)
if(NOT OpenXLSX_FOUND)
    set(OPENXLSX_CREATE_DOCS OFF CACHE BOOL "" FORCE)
    set(OPENXLSX_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(OPENXLSX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(OPENXLSX_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(OPENXLSX_ENABLE_LTO OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(OpenXLSX
        GIT_REPOSITORY https://github.com/troldal/OpenXLSX.git
        GIT_TAG c1220f3e0dc570f96fae456e69ac2eb13aa65f4d)
    FetchContent_MakeAvailable(OpenXLSX)
endif()

# qpdf removes PDF restrictions and passwords (PDF Unlocker). Use an installed copy if there is
# one (vcpkg install qpdf), else build it from source. qpdf itself needs zlib and libjpeg, which
# it only looks for as installed system libraries -- so they are downloaded and built here too
# and handed to it, and nothing has to be installed on Windows. It is built static with its own
# ("native") crypto, so there are no DLLs to ship.
find_package(qpdf CONFIG QUIET)
if(TARGET qpdf::libqpdf)
    set(FINRENAMER_QPDF_TARGET qpdf::libqpdf CACHE INTERNAL "")
else()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zlib_for_qpdf
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(zlib_for_qpdf)

    set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
    set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
    set(WITH_SIMD OFF CACHE BOOL "" FORCE)       # no NASM needed
    set(WITH_TURBOJPEG OFF CACHE BOOL "" FORCE)
    set(WITH_JAVA OFF CACHE BOOL "" FORCE)
    set(WITH_TOOLS OFF CACHE BOOL "" FORCE)
    set(WITH_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(jpeg_for_qpdf
        GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
        GIT_TAG 3.0.4
        GIT_SHALLOW TRUE
        PATCH_COMMAND ${CMAKE_COMMAND} -DFILE=<SOURCE_DIR>/CMakeLists.txt
                      -P ${PROJECT_SOURCE_DIR}/cmake/patch_libjpeg_turbo.cmake
        EXCLUDE_FROM_ALL)
    # libjpeg-turbo's install rules need these to be set (the usual defaults).
    foreach(_d "BINDIR;bin" "DATAROOTDIR;share" "DOCDIR;share/doc" "INCLUDEDIR;include" "LIBDIR;lib")
        list(GET _d 0 _name)
        list(GET _d 1 _value)
        if(NOT DEFINED CMAKE_INSTALL_${_name} OR "${CMAKE_INSTALL_${_name}}" STREQUAL "")
            set(CMAKE_INSTALL_${_name} "${_value}" CACHE STRING "" FORCE)
        endif()
    endforeach()
    FetchContent_MakeAvailable(jpeg_for_qpdf)

    # qpdf finds zlib/libjpeg with find_path/find_library; answering those cache variables
    # up front makes it use ours (a library "path" that is a target name links that target).
    set(_dep_inc "${CMAKE_BINARY_DIR}/qpdf-deps/include")
    file(MAKE_DIRECTORY "${_dep_inc}")
    foreach(_h zlib.h)
        file(COPY "${zlib_for_qpdf_SOURCE_DIR}/${_h}" DESTINATION "${_dep_inc}")
    endforeach()
    file(COPY "${zlib_for_qpdf_BINARY_DIR}/zconf.h" DESTINATION "${_dep_inc}")
    foreach(_h jpeglib.h jmorecfg.h jerror.h)
        file(COPY "${jpeg_for_qpdf_SOURCE_DIR}/${_h}" DESTINATION "${_dep_inc}")
    endforeach()
    file(COPY "${jpeg_for_qpdf_BINARY_DIR}/jconfig.h" DESTINATION "${_dep_inc}")
    set(ZLIB_H_PATH "${_dep_inc}" CACHE PATH "" FORCE)
    set(ZLIB_LIB_PATH zlibstatic CACHE STRING "" FORCE)
    set(LIBJPEG_H_PATH "${_dep_inc}" CACHE PATH "" FORCE)
    set(LIBJPEG_LIB_PATH jpeg-static CACHE STRING "" FORCE)

    set(USE_IMPLICIT_CRYPTO OFF CACHE BOOL "" FORCE)    # qpdf's built-in crypto
    set(REQUIRE_CRYPTO_NATIVE ON CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(INSTALL_MANUAL OFF CACHE BOOL "" FORCE)
    set(INSTALL_PKGCONFIG OFF CACHE BOOL "" FORCE)
    set(INSTALL_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(qpdf_src
        GIT_REPOSITORY https://github.com/qpdf/qpdf.git
        GIT_TAG v12.2.0
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
    # qpdf registers its own self-tests (they need perl); keep them out of our ctest list.
    function(add_test)
        if(NOT FINRENAMER_IN_QPDF)
            _add_test(${ARGV})
        endif()
    endfunction()
    set(FINRENAMER_IN_QPDF ON)
    FetchContent_MakeAvailable(qpdf_src)
    set(FINRENAMER_IN_QPDF OFF)
    set(FINRENAMER_QPDF_TARGET libqpdf CACHE INTERNAL "")
endif()

add_library(finrenamer_core STATIC
    src/BatesLog.cpp
    src/BatesSheet.cpp
    src/Database.cpp
    src/DirectoryListing.cpp
    src/DocumentName.cpp
    src/DeficiencyReport.cpp
    src/DocxWriter.cpp
    src/DateSpec.cpp
    src/FilenameBuilder.cpp
    src/FolderPlanner.cpp
    src/FileScanner.cpp
    src/FolderSearch.cpp
    src/NameFixer.cpp
    src/PdfUnlock.cpp
    src/RenamePlan.cpp
    src/RenameSession.cpp
    src/RenameEngine.cpp
    src/StatementCoverage.cpp
    src/StatementNames.cpp
    src/LooseStatementNames.cpp
    src/Utf8Path.cpp
    src/VacationLetters.cpp
    src/VacationSheet.cpp
)

target_include_directories(finrenamer_core PUBLIC include)

# PRIVATE: SQLiteCpp and OpenXLSX never appear in our headers, so the GUI doesn't see it.
target_link_libraries(finrenamer_core PRIVATE SQLiteCpp OpenXLSX::OpenXLSX ${FINRENAMER_QPDF_TARGET})

# pugixml (the XML reader OpenXLSX uses) is also used directly, to edit single cells of the
# Vacation Letter list without rewriting the rest of the workbook. OpenXLSX brings its own copy
# when it is downloaded; with a vcpkg-installed OpenXLSX, pugixml comes from vcpkg too.
if(NOT TARGET pugixml::pugixml)
    find_package(pugixml CONFIG QUIET)
endif()
if(TARGET pugixml::pugixml)
    target_link_libraries(finrenamer_core PRIVATE pugixml::pugixml)
else()
    message(FATAL_ERROR "FinRenamer: pugixml not found (vcpkg install pugixml)")
endif()

if(MSVC)
    # /utf-8: source and execution charset are UTF-8 (names like "José" stay intact).
    target_compile_options(finrenamer_core PUBLIC /utf-8 /permissive- /W4)
else()
    target_compile_options(finrenamer_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Run by FetchContent (PATCH_COMMAND) on the downloaded libjpeg-turbo source:
#   cmake -DFILE=<path to its CMakeLists.txt> -P patch_libjpeg_turbo.cmake
# Two small changes, nothing else:
#  1. libjpeg-turbo refuses to be built inside another CMake project (add_subdirectory) and says
#     to use ExternalProject instead. We need it as a plain static library for qpdf, so that one
#     check is switched off.
#  2. Its packaging script (cmakescripts/BuildPackages.cmake) defines global targets named
#     "installer", "dist", "rpm", ... On Windows "installer" clashes with this project's own
#     installer target (CMake error CMP0002), and we never build packages of libjpeg-turbo, so
#     that include is left out.
file(READ "${FILE}" _text)
string(FIND "${_text}" "if(FALSE) # patched" _already)
if(_already EQUAL -1)
    string(REPLACE "if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)"
                   "if(FALSE) # patched: allow add_subdirectory" _text "${_text}")
endif()
string(FIND "${_text}" "# patched: no packaging targets" _packaging)
if(_packaging EQUAL -1)
    string(REPLACE "include(cmakescripts/BuildPackages.cmake)"
                   "# patched: no packaging targets (they would clash with our installer target)"
                   _text "${_text}")
endif()
file(WRITE "${FILE}" "${_text}")

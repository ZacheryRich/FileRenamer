# Run by FetchContent (PATCH_COMMAND) on the downloaded libjpeg-turbo source:
#   cmake -DFILE=<path to its CMakeLists.txt> -P patch_libjpeg_turbo.cmake
# libjpeg-turbo refuses to be built inside another CMake project (add_subdirectory) and says to
# use ExternalProject instead. We need it as a plain static library for qpdf, so that one check
# is switched off; nothing else is changed.
file(READ "${FILE}" _text)
string(FIND "${_text}" "if(FALSE) # patched" _already)
if(_already EQUAL -1)
    string(REPLACE "if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)"
                   "if(FALSE) # patched: allow add_subdirectory" _text "${_text}")
    file(WRITE "${FILE}" "${_text}")
endif()

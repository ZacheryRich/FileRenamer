#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace finrenamer {

// On Windows, constructing std::filesystem::path from a plain std::string
// uses the ANSI code page and mangles names like "José". Always convert
// through these helpers instead.
std::filesystem::path pathFromUtf8(std::string_view utf8);
std::string utf8FromPath(const std::filesystem::path& path);

// Number of UTF-16 code units in a UTF-8 string -- this is what the Windows
// MAX_PATH (260) limit counts.
std::size_t utf16Length(std::string_view utf8);

// A comparison key for "is this the same name to Windows?". Two paths that
// Windows treats as the same file (different case, "/" vs "\") give the same key.
// On Windows this uses the OS's own case mapping, so it covers all languages;
// other platforms (used only for running tests) handle ASCII and Latin-1.
std::string caseFoldKey(const std::filesystem::path& path);

}  // namespace finrenamer

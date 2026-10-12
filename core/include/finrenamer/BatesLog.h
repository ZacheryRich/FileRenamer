#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace finrenamer {

struct BatesNumbers {
    std::string begin;
    std::string end;
};

// The Bates log Adobe Acrobat writes: one line per file, tab-separated
// "file name <TAB> beginning number <TAB> ending number".
struct BatesLog {
    // Keyed by matchKey(file name).
    std::unordered_map<std::string, BatesNumbers> entries;
    std::unordered_map<std::string, std::string> displayNames;  // key -> name as written in the log
    std::size_t lines = 0;       // usable lines read
    std::size_t duplicates = 0;  // lines whose name was already seen (the last one wins)
};

// Reads the log (UTF-8, UTF-16 with a byte-order mark, or plain ANSI text; any
// line ending). Throws std::runtime_error if the file can't be opened.
BatesLog parseBatesLog(const std::filesystem::path& file);
BatesLog parseBatesLogText(const std::string& text);

// How file names are compared between the log and the spreadsheet: control
// characters and surrounding spaces dropped, a trailing ".pdf" dropped, letter
// case ignored. Both sides use this, so "Smith 001.PDF" matches "smith 001".
std::string matchKey(const std::string& utf8Name);

}  // namespace finrenamer

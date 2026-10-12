#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace finrenamer {

// One folder and the files directly inside it.
struct FolderListing {
    std::string folder;              // "/" for the chosen folder itself, else "/Sub", "/Sub/Deeper" (UTF-8)
    std::vector<std::string> files;  // file names only (UTF-8), sorted
};

// Lists `root` and every folder below it, depth first, the chosen folder first.
// Folders and files are sorted by name (plain character order, so "Z" comes
// before "a"; this is the order of the sample Bates log sheet). Folders with no
// files directly inside them are left out. Every kind of file is listed; Office
// lock files ("~$..."), Thumbs.db, desktop.ini and .DS_Store are not.
// `skip` (optional) is one file to leave out, e.g. the spreadsheet being written
// when it sits inside the listed folder.
std::vector<FolderListing> listDirectoryTree(const std::filesystem::path& root, const std::filesystem::path& skip = {});

// True for the file names listDirectoryTree() skips.
bool isIgnoredFileName(const std::string& name);

}  // namespace finrenamer

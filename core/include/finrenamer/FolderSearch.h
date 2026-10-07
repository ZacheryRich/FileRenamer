#pragma once

#include <filesystem>
#include <vector>

namespace finrenamer {

// The folders to actually search, from the ones the user chose: made absolute,
// duplicates (ignoring letter case) removed, and anything that isn't a folder
// dropped. When searching subfolders, a chosen folder inside another chosen
// folder is dropped too, so nothing is found twice.
std::vector<std::filesystem::path> searchRoots(const std::vector<std::filesystem::path>& chosen,
                                               bool includeSubfolders);

// Every PDF (any letter case) in those folders, sorted, found once each.
std::vector<std::filesystem::path> findPdfs(const std::vector<std::filesystem::path>& chosen,
                                            bool includeSubfolders);

}  // namespace finrenamer

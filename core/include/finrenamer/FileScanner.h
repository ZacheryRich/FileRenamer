#pragma once

#include <filesystem>
#include <vector>

namespace finrenamer {

// PDFs directly inside `folder` (not recursive), matched case-insensitively
// on ".pdf", sorted by filename.
std::vector<std::filesystem::path> listPdfFiles(const std::filesystem::path& folder);

}  // namespace finrenamer

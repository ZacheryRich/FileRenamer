#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include "finrenamer/Models.h"
#include "finrenamer/RenamePlan.h"

namespace finrenamer {

struct NameFixOptions {
    bool includeSubfolders = true;  // search folders inside the chosen ones too
    bool renameFolders = true;      // rename account folders that use an old name
    std::size_t maxPathLength = 259;
};

// Marks the moves in a name-fix plan that rename a folder (PlannedMove::message).
inline constexpr const char* kFolderRenameTag = "Account folder";

// Plans the "Update File Names" job for one case over any number of folders.
//
// Everything is renamed IN PLACE -- nothing ever moves to a different folder:
//   * PDFs named "<date> <label>[ (n)].pdf", where <label> is a name one of the
//     accounts used before (a typo since fixed, an old owner display name...),
//     get the account's current label. A file named with one of the account's
//     previous numbers keeps that number.
//   * With renameFolders, a folder named with any old or current label of an
//     account that isn't its current folder name (e.g. missing "(was x1234)") is
//     itself renamed where it is, keeping its contents. If a folder with the new
//     name already exists next to it, it is left alone (status Collision).
//
// Nothing is touched; run the result with execute(). Folder renames come first
// (tagged kFolderRenameTag), then file renames, whose source paths already
// account for the folder renames. Files already named correctly aren't listed.
RenamePlan planNameFixes(const std::vector<std::filesystem::path>& roots,
                         const std::vector<Account>& accounts,
                         const std::vector<OldAccountName>& oldNames,
                         const NameFixOptions& options = {});

}  // namespace finrenamer

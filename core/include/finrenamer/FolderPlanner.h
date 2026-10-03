#pragma once

#include <filesystem>

#include "finrenamer/DateSpec.h"
#include "finrenamer/Models.h"

namespace finrenamer {

enum class FolderOrder { AccountThenYear, YearThenAccount };

struct SortOptions {
    bool byAccount = false;
    bool byYear = false;
    FolderOrder order = FolderOrder::AccountThenYear;  // used when both are on
};

// Relative subfolder (possibly empty) a file should land in, e.g.
// "Chase Checking 1234 (John Smith)/2026".
std::filesystem::path subfolderFor(const Account& account, const DateSpec& date,
                                   const SortOptions& options);

}  // namespace finrenamer

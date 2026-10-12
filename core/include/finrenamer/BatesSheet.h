#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "finrenamer/BatesLog.h"
#include "finrenamer/DirectoryListing.h"

namespace finrenamer {

// The Bates log spreadsheet (first sheet of an .xlsx):
//
//     Name | Begin Bates | End Bates         row 1: bold on grey
//                                            row 2: blank
//     /                                      folder rows: bold ("/" = the folder
//     2024.07.01 Test.pdf                    that was listed, then "/Sub", ...)
//     This is a Test.pdf                     file rows: column A
//                                            blank row between folders
//     /Folder 1
//     ...
//
// Column A is only ever added to, never edited. Columns B and C are filled in by
// applyBatesLog().

struct ListingResult {
    bool created = false;       // the workbook did not exist and was made
    std::size_t folders = 0;    // folder blocks written
    std::size_t files = 0;      // file rows written
    std::size_t firstRow = 0;   // first row written (1-based)
    std::size_t lastRow = 0;
};

// Writes the listing into `workbook`: a new workbook if the file doesn't exist,
// otherwise after whatever is already on the first sheet (one blank row between).
// Throws std::runtime_error with a message for the user if the workbook can't be
// opened (damaged, or open in Excel) or saved.
// `batesColumns` = false writes only the "Name" header (no Begin/End Bates headers),
// for a plain file list; applyBatesLog() adds the two headers later if they're missing.
ListingResult writeListing(const std::filesystem::path& workbook, const std::vector<FolderListing>& listing,
                           bool batesColumns = true);

struct BatesApplyResult {
    std::size_t fileRows = 0;                  // file rows found in column A
    std::size_t matched = 0;                   // rows given numbers
    std::vector<std::size_t> unmatchedRows;    // file rows with no entry in the log (1-based row numbers)
    std::vector<std::string> unmatchedNames;   // their names, same order
    std::vector<std::string> unusedLogNames;   // log entries that matched no row
    std::vector<std::string> sharedNames;      // names on more than one row (all of them got the same numbers)
};

// Fills Begin Bates (column B) and End Bates (column C) for every file row whose
// name is in the log (see matchKey()). Rows with no entry are left alone, so
// numbers already there stay. Adds the two headers if row 1 doesn't have them.
// Throws std::runtime_error like writeListing().
BatesApplyResult applyBatesLog(const std::filesystem::path& workbook, const BatesLog& log);

}  // namespace finrenamer

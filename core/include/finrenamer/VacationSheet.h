#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace finrenamer {

// The Vacation Letter list: the "ALL CASES" sheet of the firm's shared workbook.
// Row 1 holds the column names; every later row is one case.
//
// Reading never changes the file. Saving a case patches only the cells of that one
// row inside the .xlsx (everything else in the workbook -- other sheets, tables,
// formatting, conditional formats -- is carried over untouched), and writes through
// a temporary file so a failed save never damages the shared workbook.

struct VacationRow {
    std::size_t row = 0;                // 1-based row number in the sheet
    std::vector<std::string> values;    // one per header, "\n" for line breaks in a cell
};

struct VacationSheet {
    std::string sheetName;
    std::vector<std::string> headers;   // trimmed text of row 1
    std::vector<VacationRow> rows;      // rows with something in them, in sheet order

    // Index of a column by name ("opc address", " Client (Co-Counsel) " ...), or -1.
    // Letter case, spaces and punctuation are ignored; "OC 2 Address" counts as "OPC 2 Address".
    int column(const std::string& name) const;
};

// Someone changed the row in the workbook since it was loaded here.
class SheetConflict : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// "Client (Co-Counsel)" -> "clientcocounsel": the key used to match sheet columns to
// letter merge fields. "oc2address" is mapped to "opc2address" (older tabs use that name).
std::string normalizeHeader(const std::string& text);

// Reads the "ALL CASES..." sheet (or the first sheet if there is none).
// Throws std::runtime_error with a message for the user (missing, damaged, not an .xlsx).
VacationSheet loadVacationSheet(const std::filesystem::path& workbook);

// Writes one case. `values` has one entry per header of the sheet.
//   row > 0   : updates that row. `expected` is what the row held when it was loaded;
//               if the workbook's row differs now, throws SheetConflict and writes nothing.
//   row == 0  : adds a new case on the first free row below the last one (extending the
//               sheet's table to cover it) and ignores `expected`.
// Returns the row number written. Throws std::runtime_error if the workbook can't be saved
// (most often: it is open in Excel).
std::size_t saveVacationRow(const std::filesystem::path& workbook, std::size_t row,
                            const std::vector<std::string>& expected, const std::vector<std::string>& values);

}  // namespace finrenamer

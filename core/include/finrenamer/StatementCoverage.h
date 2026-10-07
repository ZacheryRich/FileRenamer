#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "finrenamer/DateSpec.h"
#include "finrenamer/Models.h"

namespace finrenamer {

using Month = std::chrono::year_month;

// Whole months as plain numbers (year * 12 + month - 1), for stepping and comparing.
int monthIndex(Month m);
Month monthFromIndex(int index);

// "March 2024"
std::string monthYearText(Month m);

// The months a statement date accounts for:
//   a single date -> its calendar month
//   a period      -> every month it touches (Jan 1 - Mar 31 -> Jan, Feb, Mar)
//   a quarter     -> its three months
std::vector<Month> monthsCovered(const DateSpec& date);

// What a search of the case's folders found.
struct CoverageScan {
    // Per account: the months with at least one statement file. A combined
    // statement counts for every account on it.
    std::map<std::int64_t, std::set<Month>> covered;
    std::size_t pdfCount = 0;
    std::size_t matchedCount = 0;                   // PDFs credited to an account
    std::vector<std::filesystem::path> unmatched;   // PDFs not named like a statement of this case
};

// Reads the names of every PDF in the chosen folders (renamed files only: the
// date and account come from the name, including names an account used
// before a fix and files named with an older account number).
CoverageScan scanStatements(const std::vector<std::filesystem::path>& chosenFolders,
                            bool includeSubfolders, const std::vector<Account>& accounts,
                            const std::vector<OldAccountName>& oldNames);

// One calendar year of one account, inside the report's range.
struct YearCoverage {
    int year = 0;
    std::vector<unsigned> found;    // months 1..12 with a statement
    std::vector<unsigned> missing;  // months 1..12 expected but without one
    bool notOpen = false;           // the account wasn't open at any point of this year's range
};

struct AccountCoverage {
    std::int64_t accountId = 0;
    std::string label;  // "Chase Checking 1234 (H)"
    std::optional<std::chrono::year_month_day> openedOn;
    std::optional<std::chrono::year_month_day> closedOn;
    std::vector<YearCoverage> years;  // every year from the range's start to its end
};

// Statements are expected every month from `from` to `to` (both included),
// but not before the month the account opened or after the month it closed
// (those two months themselves are expected). Empty when `to` is before `from`.
std::vector<AccountCoverage> analyzeCoverage(const std::vector<Account>& accounts,
                                             const CoverageScan& scan, Month from, Month to);

}  // namespace finrenamer

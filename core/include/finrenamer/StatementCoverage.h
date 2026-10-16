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
//   a year and month (no day) -> that month
//   a whole year  -> all twelve months
std::vector<Month> monthsCovered(const DateSpec& date);

// A file whose name has a date, an account number and the institution or type, but also
// other words ("Email from Chase to John re acct 1234"). Not credited unless the user
// says it is a statement (applyPossibleMatches).
struct PossibleMatch {
    std::filesystem::path file;
    std::int64_t accountId = 0;       // may be a combined statement's id
    DateSpec date;
    std::vector<std::string> extraWords;
};

// What a search of the case's folders found.
struct CoverageScan {
    // Per account: the months with at least one statement file. A combined
    // statement counts for every account on it.
    std::map<std::int64_t, std::set<Month>> covered;
    // The same months split by where they came from: names in this program's format
    // (strictCovered) and names read by the looser reading (looseCovered). A month can be in both.
    std::map<std::int64_t, std::set<Month>> strictCovered;
    std::map<std::int64_t, std::set<Month>> looseCovered;
    std::size_t pdfCount = 0;
    std::size_t matchedCount = 0;                   // PDFs credited to an account
    std::vector<std::filesystem::path> unmatched;   // PDFs not named like a statement of this case
    // Matched by the looser reading (LooseNameMatcher): names that aren't in this
    // program's format but show a date, an account number and the institution or type.
    std::vector<std::filesystem::path> loose;       // (also counted in matchedCount)
    // Loosely matched names with other words in them. NOT credited, NOT in matchedCount,
    // and not in `unmatched` either; the user decides each one.
    std::vector<PossibleMatch> possible;
};

// A copy of `scan` in which the possible matches with included[i] == true are credited
// (and listed in `loose`). `accounts` is every account of the case, combined ones too.
CoverageScan applyPossibleMatches(const CoverageScan& scan, const std::vector<Account>& accounts,
                                  const std::vector<bool>& included);

// Reads the names of every PDF in the chosen folders: the date and account come
// from the name. Names in this program's format come first (including names an
// account used before a fix and files named with an older account number); any
// other name is read forgivingly (see LooseNameMatcher).
CoverageScan scanStatements(const std::vector<std::filesystem::path>& chosenFolders,
                            bool includeSubfolders, const std::vector<Account>& accounts,
                            const std::vector<OldAccountName>& oldNames);

// One calendar year of one account, inside the report's range.
struct YearCoverage {
    int year = 0;
    std::vector<unsigned> found;    // months 1..12 with a statement
    std::vector<unsigned> missing;  // months 1..12 expected but without one
    std::vector<unsigned> looseOnly;  // months in `found` backed only by files not named in the program's format
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

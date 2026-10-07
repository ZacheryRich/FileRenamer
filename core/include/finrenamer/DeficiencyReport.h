#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "finrenamer/StatementCoverage.h"

namespace finrenamer {

// The Deficiency List as plain text, ready to draw as a Word document or on
// screen. Everything the reader sees is already worked out here.
struct ReportRow {
    std::string year;     // "2024", or "2021 (not open)"
    std::string found;    // "Jan–Mar, May"
    std::string missing;  // "Apr, Jun–Dec", or "None"
};

struct ReportTable {
    std::string heading;  // the account: "Chase Checking 1234 (H)"
    std::string note;     // "Opened March 15, 2022; closed November 2, 2024." (may be empty)
    std::vector<ReportRow> rows;  // one per year in the range
};

struct DeficiencyReport {
    std::string title;      // "Smith Divorce Deficiency List"
    std::string rangeLine;  // "January 2023 – Present"
    bool showFound = true;  // which columns the tables have (at least one)
    bool showMissing = true;
    std::vector<ReportTable> tables;
};

// "Jan–Mar, May": months 1..12 in order, runs of three or more joined with an
// en dash. Empty input gives "".
std::string monthListText(const std::vector<unsigned>& months);

// "March 15, 2022"
std::string longDateText(const std::chrono::year_month_day& date);

struct ReportSettings {
    Month from;
    Month to;                      // ignored in the text when throughPresent
    bool throughPresent = false;   // the range line ends with "Present"
    bool showFound = true;
    bool showMissing = true;
};

// The last month that can have a statement: the month before `today`'s.
// This is where a "Through present" range ends.
Month lastCompletedMonth(const std::chrono::year_month_day& today);

DeficiencyReport buildDeficiencyReport(const std::string& caseName,
                                       const std::vector<AccountCoverage>& coverage,
                                       const ReportSettings& settings);

// Writes the report as a Word document (.docx). Throws std::runtime_error if
// the file can't be written.
void writeDocx(const std::filesystem::path& file, const DeficiencyReport& report);

// The same document in memory (for tests).
std::string docxBytes(const DeficiencyReport& report);

}  // namespace finrenamer

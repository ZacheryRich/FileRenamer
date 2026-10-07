#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace finrenamer {

struct SingleDate {
    std::chrono::year_month_day date;
};

struct Period {
    std::chrono::year_month_day start;
    std::chrono::year_month_day end;
};

struct Quarter {
    int year = 0;
    int quarter = 1;  // 1..4
};

using DateSpec = std::variant<SingleDate, Period, Quarter>;

// Convenience constructor: makeDate(2026, 1, 31)
std::chrono::year_month_day makeDate(int year, unsigned month, unsigned day);

// "2026.01.31", "2026.01.01 - 2026.01.31", "2026.Q1"
std::string formatDateSpec(const DateSpec& spec);

// The inverse of formatDateSpec: reads "2026.01.31", "2026.01.01 - 2026.01.31"
// or "2026.Q1". Returns nullopt for anything else, including impossible dates.
std::optional<DateSpec> parseDateSpec(std::string_view text);

// Year used for year subfolders. Periods use the END date's year.
int filingYear(const DateSpec& spec);

// Returns an error message, or nullopt if the spec is valid.
std::optional<std::string> validateDateSpec(const DateSpec& spec);

// The following statement's date, for carry-forward:
//   SingleDate -> one month later (a month-end date stays month-end: Jan 31 -> Feb 28)
//   Period     -> shifted by its own length (Jan 1-Mar 31 -> Apr 1-Jun 30;
//                 Dec 15-Jan 14 -> Jan 15-Feb 14)
//   Quarter    -> the next quarter (2025 Q4 -> 2026 Q1)
DateSpec nextDateSpec(const DateSpec& spec);

// Dates as typed by the user (US order). Accepts 1/31/2026, 01-31-26,
// 01312026, 013126, 2026-01-31 and 2026.01.31. Two-digit years are 20xx.
// Returns nullopt for anything else, including impossible dates (2/30).
std::optional<std::chrono::year_month_day> parseUserDate(std::string_view text);

// "01/31/2026" -- the form parseUserDate reads back.
std::string formatUserDate(const std::chrono::year_month_day& date);

}  // namespace finrenamer

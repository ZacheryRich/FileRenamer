#pragma once

#include <chrono>
#include <optional>
#include <string>
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

// Year used for year subfolders. Periods use the END date's year.
int filingYear(const DateSpec& spec);

// Returns an error message, or nullopt if the spec is valid.
std::optional<std::string> validateDateSpec(const DateSpec& spec);

}  // namespace finrenamer

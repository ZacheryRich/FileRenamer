#include "finrenamer/DateSpec.h"

#include <cstdio>
#include <string>
#include <vector>

namespace finrenamer {

template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

namespace {

std::string formatYmd(const std::chrono::year_month_day& d)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d.%02u.%02u",
                  static_cast<int>(d.year()),
                  static_cast<unsigned>(d.month()),
                  static_cast<unsigned>(d.day()));
    return buf;
}

bool yearInRange(int y) { return y >= 1900 && y <= 9999; }

}  // namespace

std::chrono::year_month_day makeDate(int year, unsigned month, unsigned day)
{
    return std::chrono::year_month_day{std::chrono::year{year},
                                       std::chrono::month{month},
                                       std::chrono::day{day}};
}

std::string formatDateSpec(const DateSpec& spec)
{
    return std::visit(Overloaded{
        [](const SingleDate& s) { return formatYmd(s.date); },
        [](const Period& p) { return formatYmd(p.start) + " - " + formatYmd(p.end); },
        [](const Quarter& q) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%04d.Q%d", q.year, q.quarter);
            return std::string(buf);
        },
    }, spec);
}

int filingYear(const DateSpec& spec)
{
    return std::visit(Overloaded{
        [](const SingleDate& s) { return static_cast<int>(s.date.year()); },
        [](const Period& p) { return static_cast<int>(p.end.year()); },
        [](const Quarter& q) { return q.year; },
    }, spec);
}

std::optional<std::string> validateDateSpec(const DateSpec& spec)
{
    return std::visit(Overloaded{
        [](const SingleDate& s) -> std::optional<std::string> {
            if (!s.date.ok()) return "Invalid date.";
            if (!yearInRange(static_cast<int>(s.date.year()))) return "Year out of range.";
            return std::nullopt;
        },
        [](const Period& p) -> std::optional<std::string> {
            if (!p.start.ok()) return "Invalid start date.";
            if (!p.end.ok()) return "Invalid end date.";
            if (!yearInRange(static_cast<int>(p.start.year())) ||
                !yearInRange(static_cast<int>(p.end.year())))
                return "Year out of range.";
            if (std::chrono::sys_days{p.end} < std::chrono::sys_days{p.start})
                return "Period end is before its start.";
            return std::nullopt;
        },
        [](const Quarter& q) -> std::optional<std::string> {
            if (q.quarter < 1 || q.quarter > 4) return "Quarter must be 1-4.";
            if (!yearInRange(q.year)) return "Year out of range.";
            return std::nullopt;
        },
    }, spec);
}

}  // namespace finrenamer

namespace finrenamer {

using namespace std::chrono;

namespace {

year_month_day lastDayOf(const year_month& ym)
{
    return year_month_day{year_month_day_last{ym.year(), month_day_last{ym.month()}}};
}

bool isMonthEnd(const year_month_day& d)
{
    return d == lastDayOf(d.year() / d.month());
}

// Moves a date by whole months; month-end dates stay month-end and other days
// are clamped to the end of shorter months (Jan 30 -> Feb 28).
year_month_day addMonths(const year_month_day& d, int count)
{
    const year_month ym = d.year() / d.month() + months{count};
    if (isMonthEnd(d)) return lastDayOf(ym);
    const day last = lastDayOf(ym).day();
    return ym / (d.day() < last ? d.day() : last);
}

int monthIndex(const year_month_day& d)
{
    return static_cast<int>(d.year()) * 12 + static_cast<int>(static_cast<unsigned>(d.month()));
}

}  // namespace

DateSpec nextDateSpec(const DateSpec& spec)
{
    return std::visit(Overloaded{
        [](const SingleDate& s) -> DateSpec { return SingleDate{addMonths(s.date, 1)}; },
        [](const Period& p) -> DateSpec {
            const int span = monthIndex(p.end) - monthIndex(p.start);
            // Whole calendar months (1st to month-end) span one extra month.
            const bool wholeMonths = p.start.day() == day{1} && isMonthEnd(p.end);
            int step = wholeMonths ? span + 1 : span;
            if (step < 1) step = 1;
            return Period{addMonths(p.start, step), addMonths(p.end, step)};
        },
        [](const Quarter& q) -> DateSpec {
            return q.quarter >= 4 ? Quarter{q.year + 1, 1} : Quarter{q.year, q.quarter + 1};
        },
    }, spec);
}

std::optional<year_month_day> parseUserDate(std::string_view text)
{
    // Split into runs of digits; anything else is a separator.
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (c >= '0' && c <= '9') {
            current += c;
        } else if (c == '/' || c == '-' || c == '.' || c == ' ') {
            if (!current.empty()) parts.push_back(std::move(current));
            current.clear();
        } else {
            return std::nullopt;
        }
    }
    if (!current.empty()) parts.push_back(std::move(current));

    int y = 0, m = 0, d = 0;
    auto num = [](const std::string& s) { return std::stoi(s); };

    if (parts.size() == 1 && (parts[0].size() == 8 || parts[0].size() == 6)) {
        const std::string& s = parts[0];  // MMDDYYYY or MMDDYY
        m = num(s.substr(0, 2));
        d = num(s.substr(2, 2));
        y = num(s.substr(4));
        if (s.size() == 6) y += 2000;
    } else if (parts.size() == 3 && parts[0].size() == 4) {  // YYYY-MM-DD
        if (parts[1].size() > 2 || parts[2].size() > 2) return std::nullopt;
        y = num(parts[0]);
        m = num(parts[1]);
        d = num(parts[2]);
    } else if (parts.size() == 3 && parts[0].size() <= 2 && parts[1].size() <= 2 &&
               (parts[2].size() == 4 || parts[2].size() == 2)) {  // M/D/YYYY or M/D/YY
        m = num(parts[0]);
        d = num(parts[1]);
        y = num(parts[2]);
        if (parts[2].size() == 2) y += 2000;
    } else {
        return std::nullopt;
    }

    if (m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
    const year_month_day result{year{y}, month{static_cast<unsigned>(m)}, day{static_cast<unsigned>(d)}};
    if (!result.ok()) return std::nullopt;
    return result;
}

std::string formatUserDate(const year_month_day& date)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02u/%02u/%04d", static_cast<unsigned>(date.month()),
                  static_cast<unsigned>(date.day()), static_cast<int>(date.year()));
    return buf;
}

}  // namespace finrenamer

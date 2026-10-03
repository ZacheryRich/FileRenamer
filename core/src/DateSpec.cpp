#include "finrenamer/DateSpec.h"

#include <cstdio>

namespace finrenamer {
namespace {

template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

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

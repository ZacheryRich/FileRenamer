#include "finrenamer/DeficiencyReport.h"

namespace chr = std::chrono;

namespace finrenamer {
namespace {

const char* const kMonthAbbreviations[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kMonthNames[12] = {"January", "February", "March",     "April",   "May",      "June",
                                     "July",    "August",   "September", "October", "November", "December"};
const char* const kEnDash = "\xE2\x80\x93";

std::string valueText(const std::vector<unsigned>& months, bool notOpen)
{
    if (notOpen) return "\xE2\x80\x94";  // em dash
    return months.empty() ? "None" : monthListText(months);
}

}  // namespace

std::string monthListText(const std::vector<unsigned>& months)
{
    std::string out;
    for (std::size_t i = 0; i < months.size();) {
        std::size_t last = i;
        while (last + 1 < months.size() && months[last + 1] == months[last] + 1) ++last;

        auto name = [&](std::size_t k) { return std::string(kMonthAbbreviations[months[k] - 1]); };
        auto add = [&](const std::string& text) {
            if (!out.empty()) out += ", ";
            out += text;
        };
        const std::size_t run = last - i + 1;
        if (run >= 3) {
            add(name(i) + kEnDash + name(last));
        } else {
            for (std::size_t k = i; k <= last; ++k) add(name(k));
        }
        i = last + 1;
    }
    return out;
}

std::string longDateText(const chr::year_month_day& d)
{
    return std::string(kMonthNames[static_cast<unsigned>(d.month()) - 1]) + ' ' +
           std::to_string(static_cast<unsigned>(d.day())) + ", " + std::to_string(static_cast<int>(d.year()));
}

Month lastCompletedMonth(const chr::year_month_day& today)
{
    return monthFromIndex(monthIndex(Month{today.year(), today.month()}) - 1);
}

DeficiencyReport buildDeficiencyReport(const std::string& caseName,
                                       const std::vector<AccountCoverage>& coverage,
                                       const ReportSettings& settings)
{
    DeficiencyReport report;
    report.title = caseName.empty() ? std::string("Deficiency List") : caseName + " Deficiency List";
    report.rangeLine = monthYearText(settings.from) + ' ' + kEnDash + ' ' +
                       (settings.throughPresent ? std::string("Present") : monthYearText(settings.to));
    // A table needs at least one of the two columns.
    report.showFound = settings.showFound || !settings.showMissing;
    report.showMissing = settings.showMissing;

    for (const AccountCoverage& account : coverage) {
        ReportTable table;
        table.heading = account.label;
        if (account.openedOn && account.closedOn)
            table.note = "Opened " + longDateText(*account.openedOn) + "; closed " + longDateText(*account.closedOn) + ".";
        else if (account.openedOn)
            table.note = "Opened " + longDateText(*account.openedOn) + ".";
        else if (account.closedOn)
            table.note = "Closed " + longDateText(*account.closedOn) + ".";

        for (const YearCoverage& year : account.years) {
            ReportRow row;
            row.year = std::to_string(year.year) + (year.notOpen ? " (not open)" : "");
            row.found = valueText(year.found, year.notOpen);
            row.missing = valueText(year.missing, year.notOpen);
            table.rows.push_back(std::move(row));
        }
        report.tables.push_back(std::move(table));
    }
    return report;
}

}  // namespace finrenamer

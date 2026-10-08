#include "finrenamer/StatementCoverage.h"

#include <algorithm>
#include <limits>
#include <variant>

#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/FolderSearch.h"
#include "finrenamer/LooseStatementNames.h"
#include "finrenamer/StatementNames.h"
#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;
namespace chr = std::chrono;

namespace finrenamer {
namespace {

Month monthOf(const chr::year_month_day& d)
{
    return Month{d.year(), d.month()};
}

const char* const kMonthNames[12] = {"January", "February", "March",     "April",   "May",      "June",
                                     "July",    "August",   "September", "October", "November", "December"};

}  // namespace

int monthIndex(Month m)
{
    return static_cast<int>(m.year()) * 12 + static_cast<int>(static_cast<unsigned>(m.month())) - 1;
}

Month monthFromIndex(int index)
{
    return Month{chr::year{index / 12}, chr::month{static_cast<unsigned>(index % 12 + 1)}};
}

std::string monthYearText(Month m)
{
    return std::string(kMonthNames[static_cast<unsigned>(m.month()) - 1]) + ' ' +
           std::to_string(static_cast<int>(m.year()));
}

std::vector<Month> monthsCovered(const DateSpec& date)
{
    std::vector<Month> out;
    if (const auto* single = std::get_if<SingleDate>(&date)) {
        out.push_back(monthOf(single->date));
    } else if (const auto* period = std::get_if<Period>(&date)) {
        const int first = monthIndex(monthOf(period->start));
        const int last = std::max(first, monthIndex(monthOf(period->end)));
        for (int i = first; i <= last && i < first + 1200; ++i) out.push_back(monthFromIndex(i));
    } else if (const auto* quarter = std::get_if<Quarter>(&date)) {
        for (int m = 1; m <= 3; ++m)
            out.push_back(Month{chr::year{quarter->year},
                                chr::month{static_cast<unsigned>((quarter->quarter - 1) * 3 + m)}});
    }
    return out;
}

CoverageScan scanStatements(const std::vector<fs::path>& chosenFolders, bool includeSubfolders,
                            const std::vector<Account>& accounts,
                            const std::vector<OldAccountName>& oldNames)
{
    CoverageScan scan;
    const FileLabelIndex labels(accounts, oldNames);
    const LooseNameMatcher loose(accounts);

    for (const fs::path& file : findPdfs(chosenFolders, includeSubfolders)) {
        ++scan.pdfCount;
        const std::string stem = utf8FromPath(file.stem());
        const auto name = splitStatementName(stem);
        const auto entry = name ? labels.find(name->label) : std::nullopt;

        const Account* account = nullptr;
        std::optional<DateSpec> date;
        if (entry) {
            account = entry->account;
            date = name->date;
        } else if (const auto guess = loose.match(stem)) {
            account = guess->account;
            date = guess->date;
            scan.loose.push_back(file);
        }
        if (!account) {
            scan.unmatched.push_back(file);
            continue;
        }
        ++scan.matchedCount;

        const auto months = monthsCovered(*date);
        auto credit = [&](std::int64_t accountId) {
            scan.covered[accountId].insert(months.begin(), months.end());
        };
        if (account->isCombined()) {
            for (const CombinedPart& part : account->combined) credit(part.accountId);
        } else {
            credit(account->id);
        }
    }
    return scan;
}

std::vector<AccountCoverage> analyzeCoverage(const std::vector<Account>& accounts,
                                             const CoverageScan& scan, Month from, Month to)
{
    std::vector<AccountCoverage> out;
    const int fromIndex = monthIndex(from), toIndex = monthIndex(to);
    if (toIndex < fromIndex) return out;

    const std::set<Month> none;
    for (const Account& a : accounts) {
        AccountCoverage coverage;
        coverage.accountId = a.id;
        coverage.label = accountLabel(a);
        coverage.openedOn = a.openedOn;
        coverage.closedOn = a.closedOn;

        const int first = a.openedOn ? std::max(fromIndex, monthIndex(monthOf(*a.openedOn))) : fromIndex;
        const int last = a.closedOn ? std::min(toIndex, monthIndex(monthOf(*a.closedOn))) : toIndex;
        const auto statements = scan.covered.find(a.id);
        const std::set<Month>& have = statements == scan.covered.end() ? none : statements->second;

        for (int year = static_cast<int>(from.year()); year <= static_cast<int>(to.year()); ++year) {
            YearCoverage row;
            row.year = year;
            for (unsigned m = 1; m <= 12; ++m) {
                const int index = year * 12 + static_cast<int>(m) - 1;
                if (index < fromIndex || index > toIndex) continue;  // outside the report's range
                if (index < first || index > last) continue;         // account not open then
                (have.count(monthFromIndex(index)) ? row.found : row.missing).push_back(m);
            }
            row.notOpen = row.found.empty() && row.missing.empty();
            coverage.years.push_back(std::move(row));
        }
        out.push_back(std::move(coverage));
    }
    return out;
}

}  // namespace finrenamer

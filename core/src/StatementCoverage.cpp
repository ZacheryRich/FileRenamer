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

std::string key(const std::string& text)
{
    return caseFoldKey(pathFromUtf8(text));
}

// A combined statement credits the case's single accounts it covers: same institution (full
// or display name) and one of its numbers (current or older) matches a number on the statement.
std::vector<std::int64_t> accountsOnStatement(const Account& statement, const std::vector<Account>& accounts)
{
    std::vector<std::int64_t> out;
    for (const Account& a : accounts) {
        if (a.isCombined()) continue;
        const std::string inst = key(a.institution), shown = key(a.institutionDisplay);
        bool sameInstitution = false;
        for (const std::string& name : {statement.institution, statement.institutionDisplay}) {
            const std::string k = key(name);
            if (!k.empty() && (k == inst || k == shown)) sameInstitution = true;
        }
        if (!sameInstitution) continue;
        bool numberOnStatement = false;
        for (const CombinedPart& part : statement.combined) {
            const std::string n = key(part.lastFour);
            if (n == key(a.lastFour)) numberOnStatement = true;
            for (const std::string& old : a.previousLastFour)
                if (n == key(old)) numberOnStatement = true;
        }
        if (numberOnStatement) out.push_back(a.id);
    }
    return out;
}

void creditStatement(CoverageScan& scan, const std::vector<Account>& accounts, const Account& account,
                     const DateSpec& date, bool isLoose)
{
    const auto months = monthsCovered(date);
    auto credit = [&](std::int64_t accountId) {
        scan.covered[accountId].insert(months.begin(), months.end());
        (isLoose ? scan.looseCovered : scan.strictCovered)[accountId].insert(months.begin(), months.end());
    };
    if (account.isCombined()) {
        for (const std::int64_t id : accountsOnStatement(account, accounts)) credit(id);
    } else {
        credit(account.id);
    }
}

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
    } else if (const auto* ym = std::get_if<YearMonth>(&date)) {
        out.push_back(Month{chr::year{ym->year}, chr::month{static_cast<unsigned>(ym->month)}});
    } else if (const auto* year = std::get_if<WholeYear>(&date)) {
        for (unsigned m = 1; m <= 12; ++m) out.push_back(Month{chr::year{year->year}, chr::month{m}});
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
        bool isLoose = false;
        if (entry) {
            account = entry->account;
            date = name->date;
        } else if (const auto guess = loose.match(stem)) {
            if (!guess->clean()) {
                scan.possible.push_back({file, guess->account->id, guess->date, guess->extraWords});
                continue;
            }
            account = guess->account;
            date = guess->date;
            isLoose = true;
            scan.loose.push_back(file);
        }
        if (!account) {
            scan.unmatched.push_back(file);
            continue;
        }
        ++scan.matchedCount;
        creditStatement(scan, accounts, *account, *date, isLoose);
    }
    return scan;
}

CoverageScan applyPossibleMatches(const CoverageScan& scan, const std::vector<Account>& accounts,
                                  const std::vector<bool>& included)
{
    CoverageScan out = scan;
    for (std::size_t i = 0; i < scan.possible.size() && i < included.size(); ++i) {
        if (!included[i]) continue;
        const PossibleMatch& p = scan.possible[i];
        const auto it = std::find_if(accounts.begin(), accounts.end(),
                                     [&](const Account& a) { return a.id == p.accountId; });
        if (it == accounts.end()) continue;
        ++out.matchedCount;
        out.loose.push_back(p.file);
        creditStatement(out, accounts, *it, p.date, true);
    }
    return out;
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
        const auto strictIt = scan.strictCovered.find(a.id);
        const std::set<Month>& strict = strictIt == scan.strictCovered.end() ? none : strictIt->second;
        const auto looseIt = scan.looseCovered.find(a.id);
        const std::set<Month>& looseSet = looseIt == scan.looseCovered.end() ? none : looseIt->second;

        for (int year = static_cast<int>(from.year()); year <= static_cast<int>(to.year()); ++year) {
            YearCoverage row;
            row.year = year;
            for (unsigned m = 1; m <= 12; ++m) {
                const int index = year * 12 + static_cast<int>(m) - 1;
                if (index < fromIndex || index > toIndex) continue;  // outside the report's range
                if (index < first || index > last) continue;         // account not open then
                const Month month = monthFromIndex(index);
                (have.count(month) ? row.found : row.missing).push_back(m);
                if (have.count(month) && looseSet.count(month) && !strict.count(month)) row.looseOnly.push_back(m);
            }
            row.notOpen = row.found.empty() && row.missing.empty();
            coverage.years.push_back(std::move(row));
        }
        out.push_back(std::move(coverage));
    }
    return out;
}

}  // namespace finrenamer

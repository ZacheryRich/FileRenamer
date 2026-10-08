#include "finrenamer/StatementNames.h"

#include <unordered_map>

#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/Utf8Path.h"

namespace finrenamer {

std::optional<StatementName> splitStatementName(const std::string& stem)
{
    // The date forms have fixed lengths; the longest first, since a period starts
    // with a complete single date.
    for (const std::size_t length : {std::size_t{23}, std::size_t{10}, std::size_t{7}}) {
        if (stem.size() <= length + 1 || stem[length] != ' ') continue;
        if (const auto date = parseDateSpec(std::string_view(stem).substr(0, length)))
            return StatementName{stem.substr(0, length), *date, stem.substr(length + 1)};
    }
    return std::nullopt;
}

std::string FileLabelIndex::key(const std::string& utf8)
{
    return caseFoldKey(pathFromUtf8(utf8));
}

FileLabelIndex::FileLabelIndex(const std::vector<Account>& accounts,
                               const std::vector<OldAccountName>& oldNames)
{
    std::unordered_map<std::int64_t, const Account*> byId;
    for (const Account& a : accounts) byId[a.id] = &a;

    // Current labels first, so a name that is current for one account is never
    // treated as an old name of another.
    for (const Account& a : accounts) {
        labels_.emplace(key(accountLabel(a)), Entry{&a, a.lastFour});
        for (const auto& n : a.previousLastFour) labels_.emplace(key(accountLabel(a, n)), Entry{&a, n});
    }
    // Names from before numbers were written with an "x" ("Chase Checking 1234 (H)").
    for (const Account& a : accounts) {
        if (a.isCombined()) continue;
        labels_.emplace(key(legacyAccountLabel(a)), Entry{&a, a.lastFour});
        for (const auto& n : a.previousLastFour) labels_.emplace(key(legacyAccountLabel(a, n)), Entry{&a, n});
    }
    for (const OldAccountName& old : oldNames) {
        if (old.isFolder) continue;
        const auto account = byId.find(old.accountId);
        if (account != byId.end()) labels_.emplace(key(old.name), Entry{account->second, old.lastFour});
    }
}

std::optional<FileLabelIndex::Entry> FileLabelIndex::find(const std::string& label, std::string* matched) const
{
    auto found = labels_.find(key(label));
    if (found != labels_.end()) {
        if (matched) *matched = label;
        return found->second;
    }

    // "label (2)" -> "label"
    const auto open = label.rfind(" (");
    if (open == std::string::npos || label.back() != ')' || label.size() < open + 4) return std::nullopt;
    for (std::size_t i = open + 2; i + 1 < label.size(); ++i)
        if (label[i] < '0' || label[i] > '9') return std::nullopt;
    const std::string base = label.substr(0, open);
    found = labels_.find(key(base));
    if (found == labels_.end()) return std::nullopt;
    if (matched) *matched = base;
    return found->second;
}

}  // namespace finrenamer

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "finrenamer/DateSpec.h"
#include "finrenamer/Models.h"

namespace finrenamer {

// A statement's file name (without ".pdf") split the way buildFilename()
// writes it: "<date> <label>".
struct StatementName {
    std::string dateText;  // "2026.01.31", "2026.01.01 - 2026.01.31" or "2026.Q1", as written
    DateSpec date;         // the same, parsed
    std::string label;     // "Chase Checking 1234 (H)", possibly with a trailing " (2)"
};

// nullopt when the name doesn't start with a valid date and a label.
std::optional<StatementName> splitStatementName(const std::string& stem);

// Every label a case's accounts have used in file names: the current one, one
// per previous number, and the old names recorded by edits (typos fixed since,
// changed display names...). Maps a label found in a file name to its account.
class FileLabelIndex {
public:
    struct Entry {
        const Account* account = nullptr;
        std::string number;  // the number a file with this label should use ("" = current)
    };

    // `accounts` must outlive the index. Current labels win over old ones.
    FileLabelIndex(const std::vector<Account>& accounts, const std::vector<OldAccountName>& oldNames);

    // Looks a label up ignoring letter case; also accepts a trailing " (2)" added
    // for a name collision. `matched` (optional) receives the label that matched
    // without that suffix.
    std::optional<Entry> find(const std::string& label, std::string* matched = nullptr) const;

    // Every known label (as a case-folded key) and its account.
    const std::unordered_map<std::string, Entry>& entries() const { return labels_; }

    // The key labels are stored under.
    static std::string key(const std::string& utf8);

private:
    std::unordered_map<std::string, Entry> labels_;
};

}  // namespace finrenamer

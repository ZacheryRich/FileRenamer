#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace finrenamer {

// All strings in the core library are UTF-8.

struct ClientCase {
    std::int64_t id = 0;
    std::string clientName;
    std::string notes;
};

struct Person {
    std::int64_t id = 0;
    std::int64_t caseId = 0;
    std::string fullName;     // "John Smith" -- shown in the app
    std::string displayName;  // "H" -- what appears in filenames
};

// One account on a combined statement, in the order it appears in the name.
struct CombinedPart {
    std::int64_t accountId = 0;
    std::string accountType;  // "Checking"
    std::string lastFour;     // the account's current number
};

// An account as the renaming code sees it. The database layer resolves the
// account's linked Person rows into `owners` (their display names), already
// in the order they should appear in filenames.
struct Account {
    std::int64_t id = 0;
    std::int64_t caseId = 0;
    std::string institution;         // "Bank of America" -- shown in the app
    std::string institutionDisplay;  // "BofA" -- used in filenames; empty = use `institution`
    std::string accountType;   // free text: "Checking", "Roth IRA", ...
    std::string lastFour;      // "1234" -- never store the full number
    std::vector<std::string> owners;  // {"John Smith", "Jane Smith"}
    // Numbers this same account had before (a replaced card), newest first.
    // Shown in the account folder name as "(was x5678, x1234)"; a file can use
    // one of them instead of lastFour (see PlanInput::number).
    std::vector<std::string> previousLastFour;
    // Non-empty for a combined statement (one PDF covering several accounts at
    // the same institution): "Chase Checking x1111, Savings x2222 (H)". Then
    // accountType/lastFour/previousLastFour are empty, `institution`/
    // `institutionDisplay` are the member accounts' and `owners` are the ones
    // chosen for the combined statement itself.
    std::vector<CombinedPart> combined;
    // Optional. When the account was opened / closed; the Deficiency List doesn't
    // expect statements before the opening month or after the closing month.
    std::optional<std::chrono::year_month_day> openedOn;
    std::optional<std::chrono::year_month_day> closedOn;

    bool isCombined() const { return !combined.empty(); }
};

// A name an account used to have, before an edit changed it. Lets the
// "Update File Names" tool recognise files and folders named the old way.
struct OldAccountName {
    std::int64_t accountId = 0;
    bool isFolder = false;   // account folder name, or the label part of file names
    std::string name;        // e.g. "Chsae Checking 1234 (H)"
    std::string lastFour;    // for file names: the number files with that name should
                             // now use (the same number, or its corrected text)
};

}  // namespace finrenamer

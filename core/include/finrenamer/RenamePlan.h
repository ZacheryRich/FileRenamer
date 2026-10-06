#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "finrenamer/DateSpec.h"
#include "finrenamer/FolderPlanner.h"
#include "finrenamer/Models.h"

namespace finrenamer {

// What the user has entered for one file in the rename table.
struct PlanInput {
    std::filesystem::path source;
    std::optional<Account> account;
    std::optional<DateSpec> date;
    bool skip = false;   // leave this file exactly as it is
    std::string number;  // one of the account's previous numbers; empty = current
};

enum class CollisionPolicy {
    AppendNumber,  // "... (2).pdf", "... (3).pdf"
    Block,         // mark the entry as a collision and skip it
};

struct PlanOptions {
    SortOptions sort;
    CollisionPolicy collisions = CollisionPolicy::AppendNumber;
    std::size_t maxPathLength = 259;  // MAX_PATH minus the terminator
};

enum class MoveStatus {
    Ready,       // will be renamed/moved on Apply
    Unchanged,   // already has the right name and location
    Skipped,     // user chose to leave this file alone
    Incomplete,  // account or date not chosen yet
    Collision,   // destination taken (Block policy only)
    Error,       // invalid input, path too long, etc.
};

struct PlannedMove {
    std::filesystem::path source;
    std::filesystem::path destination;  // empty unless Ready/Unchanged/Collision
    MoveStatus status = MoveStatus::Incomplete;
    std::string message;                // error text, or a warning such as a numbered suffix
};

struct RenamePlan {
    std::filesystem::path root;
    std::vector<PlannedMove> moves;  // same order as the inputs

    std::size_t count(MoveStatus status) const;
    bool hasWork() const { return count(MoveStatus::Ready) > 0; }

    // True when every file is either handled or deliberately skipped, so the
    // GUI only needs to warn about leftovers when this is false.
    bool allResolved() const;
};

// Builds the preview. Touches the disk only to read (existence checks);
// nothing is renamed until RenameEngine::execute.
RenamePlan buildPlan(const std::filesystem::path& root,
                     const std::vector<PlanInput>& inputs,
                     const PlanOptions& options = {});

}  // namespace finrenamer

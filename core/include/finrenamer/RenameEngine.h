#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "finrenamer/RenamePlan.h"

namespace finrenamer {

struct ExecutedMove {
    std::filesystem::path from;
    std::filesystem::path to;
};

// Everything needed to undo one Apply. The database layer persists this as
// RenameLog rows sharing a batch id.
struct BatchRecord {
    std::string batchId;
    std::filesystem::path root;
    std::vector<ExecutedMove> moves;                    // in execution order
    std::vector<std::filesystem::path> createdFolders;  // parents before children
};

struct MoveFailure {
    std::filesystem::path path;
    std::string reason;
};

struct ExecuteResult {
    BatchRecord record;
    std::vector<MoveFailure> failures;  // e.g. file locked by a PDF viewer
};

struct UndoResult {
    std::size_t restored = 0;
    std::size_t foldersRemoved = 0;
    std::vector<MoveFailure> failures;
};

// Performs every Ready move in the plan. A failure on one file does not stop
// the others. Never overwrites an existing file.
ExecuteResult execute(const RenamePlan& plan);

// Moves files back and deletes folders this batch created, but only if they
// are empty. Files that were moved or renamed since are reported, not touched.
UndoResult undo(const BatchRecord& record);

}  // namespace finrenamer

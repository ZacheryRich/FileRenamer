#include "finrenamer/RenameEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>

#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {
namespace {

std::string makeBatchId()
{
    static std::atomic<unsigned> counter{0};
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return std::to_string(ms) + "-" + std::to_string(counter++);
}

bool sameFile(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    return fs::equivalent(a, b, ec) && !ec;
}

// Creates every missing directory between root and `dir`, recording each one.
bool ensureFolder(const fs::path& root, const fs::path& dir,
                  std::vector<fs::path>& created, std::string& error)
{
    std::vector<fs::path> missing;
    std::error_code ec;
    for (fs::path p = dir; p != root && !p.empty() && !fs::exists(p, ec); p = p.parent_path())
        missing.push_back(p);

    for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
        const bool madeIt = fs::create_directory(*it, ec);
        if (ec) {
            error = "Could not create folder: " + ec.message();
            return false;
        }
        // false without an error means it appeared in the meantime -- it isn't
        // ours, so don't record it (undo must never remove it).
        if (madeIt) created.push_back(*it);
    }
    return true;
}

void removeEmptyFolders(std::vector<fs::path>& folders, std::size_t* removedCount)
{
    // Children were created after parents, so walk backwards.
    std::vector<fs::path> kept;
    for (auto it = folders.rbegin(); it != folders.rend(); ++it) {
        std::error_code ec;
        if (fs::is_directory(*it, ec) && fs::is_empty(*it, ec) && fs::remove(*it, ec)) {
            if (removedCount) ++*removedCount;
        } else if (fs::exists(*it, ec)) {
            kept.push_back(*it);
        }
    }
    std::reverse(kept.begin(), kept.end());
    folders = std::move(kept);
}

}  // namespace

ExecuteResult execute(const RenamePlan& plan)
{
    ExecuteResult result;
    result.record.batchId = makeBatchId();
    result.record.root = plan.root;

    for (const PlannedMove& move : plan.moves) {
        if (move.status != MoveStatus::Ready) continue;

        std::string error;
        if (!ensureFolder(plan.root, move.destination.parent_path(),
                          result.record.createdFolders, error)) {
            result.failures.push_back({move.source, error});
            continue;
        }

        // Re-check right before moving: std::filesystem::rename replaces an
        // existing file on Windows, and something may have appeared since planning.
        std::error_code ec;
        if (fs::exists(move.destination, ec) && !sameFile(move.destination, move.source)) {
            result.failures.push_back({move.source, "Destination already exists."});
            continue;
        }

        fs::rename(move.source, move.destination, ec);
        if (ec) {
            result.failures.push_back({move.source, ec.message()});
            continue;
        }
        result.record.moves.push_back({move.source, move.destination});
    }

    // Folders created for moves that then failed are left empty -- tidy them.
    removeEmptyFolders(result.record.createdFolders, nullptr);
    return result;
}

std::size_t removeEmptiedFolders(const BatchRecord& record)
{
    std::size_t removed = 0;
    std::error_code ec;
    for (const ExecutedMove& m : record.moves) {
        for (fs::path dir = m.from.parent_path();
             !dir.empty() && dir != record.root && dir != dir.parent_path();
             dir = dir.parent_path()) {
            if (!fs::is_directory(dir, ec) || !fs::is_empty(dir, ec)) break;
            if (!fs::remove(dir, ec)) break;
            ++removed;
        }
    }
    return removed;
}

UndoResult undo(const BatchRecord& record)
{
    UndoResult result;

    for (auto it = record.moves.rbegin(); it != record.moves.rend(); ++it) {
        std::error_code ec;
        if (!fs::exists(it->to, ec)) {
            result.failures.push_back({it->to, "File is no longer where it was moved to."});
            continue;
        }
        if (fs::exists(it->from, ec) && !sameFile(it->from, it->to)) {
            result.failures.push_back({it->from, "Another file now has the original name."});
            continue;
        }
        // The original folder may have been removed after it was emptied
        // (removeEmptiedFolders); bring it back.
        fs::create_directories(it->from.parent_path(), ec);
        fs::rename(it->to, it->from, ec);
        if (ec) {
            result.failures.push_back({it->to, ec.message()});
            continue;
        }
        ++result.restored;
    }

    std::vector<fs::path> folders = record.createdFolders;
    removeEmptyFolders(folders, &result.foldersRemoved);
    return result;
}

}  // namespace finrenamer

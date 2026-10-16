#include "finrenamer/RenamePlan.h"

#include <algorithm>
#include <unordered_set>

#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {
namespace {

// Windows paths are case-insensitive, so compare on a case-folded key.
std::string pathKey(const fs::path& p)
{
    return caseFoldKey(p);
}

bool sameFile(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    return fs::equivalent(a, b, ec) && !ec;
}

bool isInside(const fs::path& root, const fs::path& p)
{
    const fs::path rel = p.lexically_relative(root);
    return !rel.empty() && *rel.begin() != "..";
}

}  // namespace

std::size_t RenamePlan::count(MoveStatus status) const
{
    return static_cast<std::size_t>(std::count_if(
        moves.begin(), moves.end(), [status](const PlannedMove& m) { return m.status == status; }));
}

bool RenamePlan::allResolved() const
{
    return std::all_of(moves.begin(), moves.end(), [](const PlannedMove& m) {
        return m.status == MoveStatus::Ready || m.status == MoveStatus::Unchanged ||
               m.status == MoveStatus::Skipped;
    });
}

RenamePlan buildPlan(const fs::path& rootIn, const std::vector<PlanInput>& inputs,
                     const PlanOptions& options)
{
    RenamePlan plan;
    std::error_code ec;
    plan.root = fs::weakly_canonical(fs::absolute(rootIn), ec);
    if (ec) plan.root = fs::absolute(rootIn).lexically_normal();

    std::unordered_set<std::string> seenSources;
    std::unordered_set<std::string> claimed;  // destinations taken by earlier entries

    for (const PlanInput& input : inputs) {
        PlannedMove move;
        move.source = fs::weakly_canonical(fs::absolute(input.source), ec);
        if (ec) move.source = fs::absolute(input.source).lexically_normal();

        auto fail = [&](MoveStatus status, std::string message) {
            move.status = status;
            move.message = std::move(message);
            plan.moves.push_back(move);
        };

        if (!seenSources.insert(pathKey(move.source)).second) {
            fail(MoveStatus::Error, "File is listed twice.");
            continue;
        }
        if (input.skip) {
            // No validation and no destination: the file keeps its current name,
            // which other files still can't take (the on-disk check sees it).
            move.status = MoveStatus::Skipped;
            plan.moves.push_back(move);
            continue;
        }
        if (!isInside(plan.root, move.source)) {
            fail(MoveStatus::Error, "File is outside the selected folder.");
            continue;
        }
        if (!fs::is_regular_file(move.source, ec)) {
            fail(MoveStatus::Error, "File no longer exists.");
            continue;
        }
        const bool isDocument = input.document && input.document->kind != DocKind::Statement;
        if (isDocument) {
            if (auto missing = documentMissing(*input.document, !input.date)) {
                fail(MoveStatus::Incomplete, *missing);
                continue;
            }
            if (input.date && documentUsesDate(*input.document))
                if (auto err = validateDocumentDate(*input.document, *input.date)) {
                    fail(MoveStatus::Error, *err);
                    continue;
                }
        } else if (!input.account || !input.date) {
            fail(MoveStatus::Incomplete, {});
            continue;
        }
        if (!isDocument) {
            if (auto err = validateAccount(*input.account)) {
                fail(MoveStatus::Error, *err);
                continue;
            }
        }
        const bool dated = !isDocument || documentUsesDate(*input.document);
        if (dated)
            if (auto err = validateDateSpec(*input.date)) {
                fail(MoveStatus::Error, *err);
                continue;
            }

        // Account and year folders are for statements only; other files stay where they are.
        const fs::path folder = isDocument ? plan.root
                                           : plan.root / subfolderFor(*input.account, *input.date, options.sort);
        const std::string ext = utf8FromPath(move.source.extension());
        const std::string stem = isDocument ? buildDocumentFilename(*input.document, input.date, "")
                                            : buildFilename(*input.account, *input.date, "", input.number);

        if (isReservedWindowsName(stem + ext)) {
            fail(MoveStatus::Error, "Windows doesn't allow a file named \"" + stem + ext +
                                        "\". Add more to the name.");
            continue;
        }

        // Find the first free name: "stem.pdf", "stem (2).pdf", ...
        fs::path dest;
        bool found = false;
        for (int n = 1; n < 1000; ++n) {
            const std::string name = n == 1 ? stem + ext
                                            : stem + " (" + std::to_string(n) + ")" + ext;
            dest = folder / pathFromUtf8(name);
            const bool takenInBatch = claimed.count(pathKey(dest)) > 0;
            const bool takenOnDisk = fs::exists(dest, ec) && !sameFile(dest, move.source);
            if (!takenInBatch && !takenOnDisk) { found = true; break; }
            if (options.collisions == CollisionPolicy::Block) break;
        }

        move.destination = dest;
        if (!found) {
            fail(MoveStatus::Collision,
                 options.collisions == CollisionPolicy::Block
                     ? "Another file already has this name."
                     : "Could not find a free name.");
            continue;
        }

        // Check the length before claiming, so a rejected row doesn't reserve
        // a name and push a later file to "(2)".
        if (utf16Length(utf8FromPath(dest)) > options.maxPathLength) {
            fail(MoveStatus::Error, "Full path is longer than Windows allows.");
            continue;
        }

        claimed.insert(pathKey(dest));

        if (dest == move.source) {
            move.status = MoveStatus::Unchanged;
        } else {
            move.status = MoveStatus::Ready;
            if (dest.filename() != pathFromUtf8(stem + ext))
                move.message = "Name was taken; a number was added.";
        }
        plan.moves.push_back(move);
    }
    return plan;
}

}  // namespace finrenamer

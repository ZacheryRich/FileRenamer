#include "finrenamer/RenameSession.h"

#include <unordered_set>

#include "finrenamer/FileScanner.h"
#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {

namespace {

// Paths are compared the way Windows does (ignoring letter case).
bool samePath(const fs::path& a, const fs::path& b)
{
    return caseFoldKey(a) == caseFoldKey(b);
}

// Resolve the folder the same way buildPlan does, so the paths we list match
// the paths the planner and engine report back.
fs::path resolveRoot(const fs::path& root)
{
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(fs::absolute(root), ec);
    return ec ? fs::absolute(root).lexically_normal() : resolved;
}

}  // namespace

RenameSession::RenameSession(fs::path root, std::vector<Account> accounts)
    : root_(resolveRoot(root)), accounts_(std::move(accounts))
{
    refresh();
}

void RenameSession::setAccounts(std::vector<Account> accounts)
{
    accounts_ = std::move(accounts);
}

const Account* RenameSession::findAccount(std::int64_t id) const
{
    for (const Account& a : accounts_)
        if (a.id == id) return &a;
    return nullptr;
}

void RenameSession::refresh()
{
    std::error_code ec;

    // Drop rows whose file is gone (renamed or deleted outside the program).
    std::vector<SessionRow> kept;
    for (SessionRow& r : rows_)
        if (r.done || fs::exists(r.path, ec)) kept.push_back(std::move(r));
    rows_ = std::move(kept);

    std::unordered_set<std::string> known;
    for (const SessionRow& r : rows_) known.insert(caseFoldKey(r.path));

    for (const fs::path& file : listPdfFiles(root_)) {
        if (known.count(caseFoldKey(file))) continue;
        SessionRow r;
        r.path = file;
        r.originalPath = file;
        rows_.push_back(std::move(r));
    }
}

std::vector<PlanInput> RenameSession::pendingInputs(std::vector<std::size_t>& rowIndex) const
{
    std::vector<PlanInput> inputs;
    rowIndex.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const SessionRow& r = rows_[i];
        if (r.done) continue;

        PlanInput in;
        in.source = r.path;
        in.skip = r.skip;
        in.date = r.date;
        if (r.accountId)
            if (const Account* a = findAccount(*r.accountId)) in.account = *a;
        inputs.push_back(std::move(in));
        rowIndex.push_back(i);
    }
    return inputs;
}

std::vector<PlannedMove> RenameSession::preview(const PlanOptions& options) const
{
    std::vector<PlannedMove> out(rows_.size());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].done) continue;
        out[i].source = rows_[i].path;
        out[i].destination = rows_[i].path;
        out[i].status = MoveStatus::Unchanged;
    }

    std::vector<std::size_t> rowIndex;
    const RenamePlan plan = buildPlan(root_, pendingInputs(rowIndex), options);
    for (std::size_t k = 0; k < plan.moves.size(); ++k) out[rowIndex[k]] = plan.moves[k];
    return out;
}

bool RenameSession::carryForward(std::size_t from, std::size_t to)
{
    if (from >= rows_.size() || to >= rows_.size() || from == to) return false;
    const SessionRow& src = rows_[from];
    SessionRow& dst = rows_[to];
    if (dst.done || !dst.isBlank() || src.skip) return false;
    if (!src.accountId && !src.date) return false;

    dst.accountId = src.accountId;
    if (src.date) dst.date = nextDateSpec(*src.date);
    return true;
}

std::optional<std::size_t> RenameSession::nextPending(std::size_t from) const
{
    for (std::size_t i = from + 1; i < rows_.size(); ++i)
        if (!rows_[i].done) return i;
    return std::nullopt;
}

bool RenameSession::hasUnappliedEntries() const
{
    for (const SessionRow& r : rows_)
        if (!r.done && (r.accountId || r.date)) return true;
    return false;
}

ExecuteResult RenameSession::apply(const PlanOptions& options)
{
    std::vector<std::size_t> rowIndex;
    const RenamePlan plan = buildPlan(root_, pendingInputs(rowIndex), options);
    ExecuteResult result = execute(plan);

    for (std::size_t k = 0; k < plan.moves.size(); ++k) {
        if (plan.moves[k].status == MoveStatus::Ready) rows_[rowIndex[k]].failure.clear();
    }
    for (const ExecutedMove& m : result.record.moves) {
        for (SessionRow& r : rows_) {
            if (!r.done && samePath(r.path, m.from)) {
                r.path = m.to;
                r.done = true;
                break;
            }
        }
    }
    for (const MoveFailure& f : result.failures) {
        for (SessionRow& r : rows_)
            if (!r.done && samePath(r.path, f.path)) r.failure = f.reason;
    }

    if (!result.record.moves.empty()) batches_.push_back(result.record);
    return result;
}

std::optional<SessionUndoResult> RenameSession::undoLast()
{
    if (batches_.empty()) return std::nullopt;
    const BatchRecord record = batches_.back();
    batches_.pop_back();

    SessionUndoResult out{record.batchId, undo(record)};

    std::error_code ec;
    for (const ExecutedMove& m : record.moves) {
        // Restored = back at the original name and gone from the new one.
        if (!fs::exists(m.from, ec) || fs::exists(m.to, ec)) continue;
        for (SessionRow& r : rows_) {
            if (r.done && samePath(r.path, m.to)) {
                r.path = m.from;
                r.done = false;
                break;
            }
        }
    }
    return out;
}

}  // namespace finrenamer

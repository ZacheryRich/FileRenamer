#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "finrenamer/DateSpec.h"
#include "finrenamer/DocumentName.h"
#include "finrenamer/Models.h"
#include "finrenamer/RenameEngine.h"
#include "finrenamer/RenamePlan.h"

namespace finrenamer {

// One PDF on the rename screen.
struct SessionRow {
    std::filesystem::path path;          // where the file is now
    std::filesystem::path originalPath;  // where it was when first listed
    std::optional<std::int64_t> accountId;
    std::string number;  // a previous number of that account; empty = current
    // An email, text, letter or other file (kind != Statement): named from this
    // instead of the account. A statement keeps kind Statement.
    DocumentInfo document;
    std::optional<DateSpec> date;
    bool skip = false;
    bool done = false;     // renamed during this session; `path` is the new location
    std::string failure;   // why the last Apply couldn't rename it, if it failed

    bool isBlank() const { return !accountId && !date && !skip && document.isBlank(); }
    bool isDocument() const { return document.kind != DocKind::Statement; }
};

struct SessionUndoResult {
    std::string batchId;
    UndoResult result;
};

// Everything the rename screen keeps track of for one folder: which files are
// listed, what has been entered for each, and which have been renamed.
// Files are never touched except by apply() and undoLast().
class RenameSession {
public:
    // Lists the PDFs directly inside `root`.
    RenameSession(std::filesystem::path root, std::vector<Account> accounts);

    const std::filesystem::path& root() const { return root_; }

    const std::vector<Account>& accounts() const { return accounts_; }
    void setAccounts(std::vector<Account> accounts);  // e.g. after adding an account
    const Account* findAccount(std::int64_t id) const;

    std::size_t size() const { return rows_.size(); }
    const std::vector<SessionRow>& rows() const { return rows_; }
    SessionRow& row(std::size_t i) { return rows_.at(i); }
    const SessionRow& row(std::size_t i) const { return rows_.at(i); }

    // Re-reads the folder. New PDFs are added at the end; rows whose files have
    // disappeared are removed (unless already renamed). Entries are kept.
    void refresh();

    // Preview for every row, same order as rows(). Renamed rows report
    // Unchanged with their new location.
    std::vector<PlannedMove> preview(const PlanOptions& options) const;

    // Carry-forward: if row `to` is blank, give it row `from`'s account (and
    // chosen number) and the following date (see nextDateSpec). After an email,
    // template or other file it copies the kind, template, typed text and the SAME
    // date instead (the optional boxes, e.g. a description, are left blank, and the next
    // file is more likely the same day than a month later). Returns true if anything was
    // filled in.
    bool carryForward(std::size_t from, std::size_t to);

    // After templates were added, edited or deleted: rows (not yet renamed) that use one get the
    // current version, keeping what was typed. A row whose template is gone keeps its text as an
    // "Other" file name (or goes back to a statement row if nothing was typed).
    void refreshTemplates(const std::vector<DocumentTemplate>& templates);

    // Index of the next row after `from` that hasn't been renamed, if any.
    std::optional<std::size_t> nextPending(std::size_t from) const;

    // True if any not-yet-renamed row has an account or date entered.
    bool hasUnappliedEntries() const;

    // Renames every Ready row. Rows that moved become done; rows that failed get
    // `failure` set. The batch is remembered for undoLast().
    ExecuteResult apply(const PlanOptions& options);

    // Undoes the most recent apply() of this session; restored rows become
    // editable again with their entries intact. Empty if there is nothing to undo.
    std::optional<SessionUndoResult> undoLast();
    bool canUndo() const { return !batches_.empty(); }

private:
    std::vector<PlanInput> pendingInputs(std::vector<std::size_t>& rowIndex) const;

    std::filesystem::path root_;
    std::vector<Account> accounts_;
    std::vector<SessionRow> rows_;
    std::vector<BatchRecord> batches_;
};

}  // namespace finrenamer

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "finrenamer/Models.h"
#include "finrenamer/RenameEngine.h"

namespace finrenamer {

// Thrown for rule violations the user can fix ("this person still owns an
// account"). Its message is meant to be shown in the GUI as-is.
// Low-level SQLite failures (disk full, file locked) are thrown as
// std::runtime_error, so the GUI can simply catch std::exception.
class DatabaseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// An account as it is stored: owners are Person ids, in display order.
// Use Database::loadAccounts() to get the renaming-ready Account form.
struct AccountRecord {
    std::int64_t id = 0;
    std::int64_t caseId = 0;
    std::string institution;
    std::string accountType;
    std::string lastFour;
    std::vector<std::int64_t> ownerIds;
};

// One row of the rename history list.
struct BatchSummary {
    std::string batchId;
    std::optional<std::int64_t> caseId;  // empty if the case was deleted since
    std::filesystem::path root;
    std::string createdAt;  // UTC, "2026-10-03T18:09:00Z"
    std::size_t moveCount = 0;
    bool undone = false;
};

class Database {
public:
    // Opens (creating if needed) the database file and brings its schema up
    // to date. The parent folder must exist.
    explicit Database(const std::filesystem::path& file);

    // A throwaway database that lives only in memory (for tests).
    static Database openInMemory();

    ~Database();
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Schema version stored in the file (PRAGMA user_version).
    int schemaVersion() const;

    // ---- Cases ----------------------------------------------------------
    std::int64_t createCase(const ClientCase& c);  // returns the new id; c.id is ignored
    void updateCase(const ClientCase& c);
    void deleteCase(std::int64_t caseId);          // also deletes its people and accounts
    std::optional<ClientCase> getCase(std::int64_t caseId) const;
    std::vector<ClientCase> listCases() const;     // sorted by client name

    // ---- People ---------------------------------------------------------
    std::int64_t addPerson(std::int64_t caseId, const std::string& fullName);
    void renamePerson(std::int64_t personId, const std::string& fullName);
    void deletePerson(std::int64_t personId);      // refused while they own an account
    std::vector<Person> listPeople(std::int64_t caseId) const;  // sorted by name

    // ---- Accounts -------------------------------------------------------
    std::int64_t createAccount(const AccountRecord& a);  // a.id is ignored
    void updateAccount(const AccountRecord& a);          // replaces owners too
    void deleteAccount(std::int64_t accountId);
    std::optional<AccountRecord> getAccount(std::int64_t accountId) const;
    std::vector<AccountRecord> listAccountRecords(std::int64_t caseId) const;

    // Accounts with owner names filled in, ready for FilenameBuilder/RenamePlan.
    // Sorted by institution, type, last four.
    std::vector<Account> loadAccounts(std::int64_t caseId) const;

    // Every account type used so far, across all cases, for the type field's
    // autocomplete. Case-insensitive duplicates are merged.
    std::vector<std::string> accountTypeSuggestions() const;

    // ---- Rename history -------------------------------------------------
    void saveBatch(const BatchRecord& batch, std::optional<std::int64_t> caseId);
    std::optional<BatchRecord> loadBatch(const std::string& batchId) const;
    std::vector<BatchSummary> listBatches(std::size_t limit = 100) const;  // newest first
    void markBatchUndone(const std::string& batchId);

private:
    struct Impl;
    explicit Database(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace finrenamer

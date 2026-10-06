#include "finrenamer/Database.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <set>

#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {

namespace {

constexpr int kSchemaVersion = 4;

// Each entry upgrades the schema by one version. Never edit a shipped entry;
// add a new one instead, so existing databases upgrade in place.
const char* const kMigrations[] = {
    // Version 1
    R"sql(
    CREATE TABLE cases (
        id          INTEGER PRIMARY KEY,
        client_name TEXT NOT NULL,
        notes       TEXT NOT NULL DEFAULT ''
    );

    CREATE TABLE people (
        id        INTEGER PRIMARY KEY,
        case_id   INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
        full_name TEXT NOT NULL COLLATE NOCASE,
        UNIQUE (case_id, full_name)
    );

    CREATE TABLE accounts (
        id           INTEGER PRIMARY KEY,
        case_id      INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
        institution  TEXT NOT NULL,
        account_type TEXT NOT NULL,
        last_four    TEXT NOT NULL
    );

    CREATE TABLE account_owners (
        account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        person_id  INTEGER NOT NULL REFERENCES people(id) ON DELETE RESTRICT,
        position   INTEGER NOT NULL,
        PRIMARY KEY (account_id, person_id)
    );

    CREATE TABLE rename_batches (
        batch_id   TEXT PRIMARY KEY,
        case_id    INTEGER REFERENCES cases(id) ON DELETE SET NULL,
        root       TEXT NOT NULL,
        created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
        undone     INTEGER NOT NULL DEFAULT 0
    );

    CREATE TABLE rename_log (
        batch_id  TEXT NOT NULL REFERENCES rename_batches(batch_id) ON DELETE CASCADE,
        seq       INTEGER NOT NULL,
        from_path TEXT NOT NULL,
        to_path   TEXT NOT NULL,
        PRIMARY KEY (batch_id, seq)
    );

    CREATE TABLE created_folders (
        batch_id TEXT NOT NULL REFERENCES rename_batches(batch_id) ON DELETE CASCADE,
        seq      INTEGER NOT NULL,
        path     TEXT NOT NULL,
        PRIMARY KEY (batch_id, seq)
    );

    CREATE INDEX idx_people_case   ON people(case_id);
    CREATE INDEX idx_accounts_case ON accounts(case_id);
    CREATE INDEX idx_owners_person ON account_owners(person_id);
    )sql",

    // Version 2: separate display name (used in filenames) for each person.
    // Existing people keep showing their full name until edited.
    R"sql(
    ALTER TABLE people ADD COLUMN display_name TEXT NOT NULL DEFAULT '' COLLATE NOCASE;
    UPDATE people SET display_name = full_name;
    CREATE UNIQUE INDEX idx_people_display ON people(case_id, display_name);
    )sql",

    // Version 3: display name (abbreviation) for each account's institution.
    R"sql(
    ALTER TABLE accounts ADD COLUMN institution_display TEXT NOT NULL DEFAULT '';
    UPDATE accounts SET institution_display = institution;
    )sql",

    // Version 4: previous numbers of an account (replaced cards), and the names
    // accounts had before edits, so old file and folder names can be found.
    R"sql(
    CREATE TABLE account_previous_numbers (
        account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        last_four  TEXT NOT NULL COLLATE NOCASE,
        position   INTEGER NOT NULL,
        PRIMARY KEY (account_id, last_four)
    );

    CREATE TABLE account_name_history (
        account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        is_folder   INTEGER NOT NULL,
        name        TEXT NOT NULL COLLATE NOCASE,
        last_four   TEXT NOT NULL DEFAULT '',
        recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
        PRIMARY KEY (account_id, is_folder, name)
    );
    )sql",
};

static_assert(std::size(kMigrations) == kSchemaVersion,
              "Add one migration per schema version");

std::string trimmed(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// A blank institution display name is stored as the institution itself.
std::string institutionDisplayOrDefault(const AccountRecord& a)
{
    std::string shown = trimmed(a.institutionDisplay);
    return shown.empty() ? trimmed(a.institution) : shown;
}

}  // namespace

// ---------------------------------------------------------------------------

struct Database::Impl {
    explicit Impl(const std::string& utf8File)
        : db(utf8File, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
    {
        db.exec("PRAGMA foreign_keys = ON");
        db.setBusyTimeout(3000);  // wait briefly if another copy of the app is writing
        migrate();
    }

    SQLite::Database db;

    int userVersion()
    {
        return db.execAndGet("PRAGMA user_version").getInt();
    }

    void migrate()
    {
        const int current = userVersion();
        if (current > kSchemaVersion)
            throw DatabaseError("This database was created by a newer version of the program.");

        for (int v = current; v < kSchemaVersion; ++v) {
            SQLite::Transaction tx(db);
            db.exec(kMigrations[v]);
            db.exec("PRAGMA user_version = " + std::to_string(v + 1));
            tx.commit();
        }
    }

    // ---- shared checks ----

    void requireCase(std::int64_t caseId)
    {
        SQLite::Statement q(db, "SELECT 1 FROM cases WHERE id = ?");
        q.bind(1, caseId);
        if (!q.executeStep()) throw DatabaseError("That case no longer exists.");
    }

    // Full names and display names are each unique within a case (ignoring
    // letter case), so no two people can produce the same filename.
    void requireUniquePerson(std::int64_t caseId, const std::string& fullName,
                             const std::string& displayName, std::int64_t exceptId)
    {
        SQLite::Statement full(db,
            "SELECT 1 FROM people WHERE case_id = ? AND full_name = ? AND id <> ?");
        full.bind(1, caseId);
        full.bind(2, fullName);
        full.bind(3, exceptId);
        if (full.executeStep())
            throw DatabaseError("\"" + fullName + "\" is already listed on this case.");

        SQLite::Statement shown(db,
            "SELECT full_name FROM people WHERE case_id = ? AND display_name = ? AND id <> ?");
        shown.bind(1, caseId);
        shown.bind(2, displayName);
        shown.bind(3, exceptId);
        if (shown.executeStep())
            throw DatabaseError(shown.getColumn(0).getString() + " already shows as \"" +
                                displayName + "\" in filenames. Choose a different display name.");
    }

    // Trims both names and fills in a blank display name. Throws if the full name is blank.
    static std::pair<std::string, std::string> cleanNames(const std::string& fullName,
                                                          const std::string& displayName)
    {
        std::string full = trimmed(fullName);
        if (full.empty()) throw DatabaseError("Name is required.");
        std::string shown = trimmed(displayName);
        if (shown.empty()) shown = full;
        return {std::move(full), std::move(shown)};
    }

    std::int64_t insertPerson(std::int64_t caseId, const std::string& fullName,
                              const std::string& displayName)
    {
        auto [full, shown] = cleanNames(fullName, displayName);
        requireUniquePerson(caseId, full, shown, 0);

        SQLite::Statement q(db,
            "INSERT INTO people (case_id, full_name, display_name) VALUES (?, ?, ?)");
        q.bind(1, caseId);
        q.bind(2, full);
        q.bind(3, shown);
        q.exec();
        return db.getLastInsertRowid();
    }

    void validate(const AccountRecord& a)
    {
        Account probe;
        probe.institution = a.institution;
        probe.accountType = a.accountType;
        probe.lastFour = a.lastFour;
        if (auto err = validateAccount(probe)) throw DatabaseError(*err);

        std::set<std::string> numbers{caseFoldKey(pathFromUtf8(trimmed(a.lastFour)))};
        for (const auto& n : a.previousLastFour) {
            const std::string clean = trimmed(n);
            if (clean.empty()) throw DatabaseError("A previous number is blank.");
            if (!numbers.insert(caseFoldKey(pathFromUtf8(clean))).second)
                throw DatabaseError("The number " + clean + " is listed more than once.");
        }

        std::set<std::int64_t> seen;
        for (const auto id : a.ownerIds) {
            if (!seen.insert(id).second)
                throw DatabaseError("The same person is listed twice as an owner.");

            SQLite::Statement q(db, "SELECT case_id FROM people WHERE id = ?");
            q.bind(1, id);
            if (!q.executeStep()) throw DatabaseError("An owner no longer exists.");
            if (q.getColumn(0).getInt64() != a.caseId)
                throw DatabaseError("An owner belongs to a different case.");
        }
    }

    void writeOwners(std::int64_t accountId, const std::vector<std::int64_t>& ownerIds)
    {
        SQLite::Statement del(db, "DELETE FROM account_owners WHERE account_id = ?");
        del.bind(1, accountId);
        del.exec();

        SQLite::Statement ins(db,
            "INSERT INTO account_owners (account_id, person_id, position) VALUES (?, ?, ?)");
        int position = 0;
        for (const auto personId : ownerIds) {
            ins.bind(1, accountId);
            ins.bind(2, personId);
            ins.bind(3, position++);
            ins.exec();
            ins.reset();
        }
    }

    std::vector<std::int64_t> ownersOf(std::int64_t accountId)
    {
        SQLite::Statement q(db,
            "SELECT person_id FROM account_owners WHERE account_id = ? ORDER BY position");
        q.bind(1, accountId);
        std::vector<std::int64_t> ids;
        while (q.executeStep()) ids.push_back(q.getColumn(0).getInt64());
        return ids;
    }

    void writePreviousNumbers(std::int64_t accountId, const std::vector<std::string>& numbers)
    {
        SQLite::Statement del(db, "DELETE FROM account_previous_numbers WHERE account_id = ?");
        del.bind(1, accountId);
        del.exec();

        SQLite::Statement ins(db,
            "INSERT INTO account_previous_numbers (account_id, last_four, position) VALUES (?, ?, ?)");
        int position = 0;
        for (const auto& n : numbers) {
            ins.bind(1, accountId);
            ins.bind(2, trimmed(n));
            ins.bind(3, position++);
            ins.exec();
            ins.reset();
        }
    }

    std::vector<std::string> previousOf(std::int64_t accountId)
    {
        SQLite::Statement q(db,
            "SELECT last_four FROM account_previous_numbers WHERE account_id = ? ORDER BY position");
        q.bind(1, accountId);
        std::vector<std::string> out;
        while (q.executeStep()) out.push_back(q.getColumn(0).getString());
        return out;
    }

    // The renaming form of an account: owners' display names, previous numbers.
    Account toAccount(const AccountRecord& r)
    {
        Account a;
        a.id = r.id;
        a.caseId = r.caseId;
        a.institution = r.institution;
        a.institutionDisplay = r.institutionDisplay;
        a.accountType = r.accountType;
        a.lastFour = r.lastFour;
        a.previousLastFour = r.previousLastFour;

        SQLite::Statement names(db, R"sql(
            SELECT p.display_name FROM account_owners o JOIN people p ON p.id = o.person_id
            WHERE o.account_id = ? ORDER BY o.position)sql");
        names.bind(1, r.id);
        while (names.executeStep()) a.owners.push_back(names.getColumn(0).getString());
        return a;
    }

    std::optional<Account> loadAccount(std::int64_t accountId)
    {
        SQLite::Statement q(db,
            "SELECT id, case_id, institution, account_type, last_four, institution_display"
            " FROM accounts WHERE id = ?");
        q.bind(1, accountId);
        if (!q.executeStep()) return std::nullopt;
        AccountRecord r = readAccountRow(q);
        r.previousLastFour = previousOf(r.id);
        return toAccount(r);
    }

    // Remembers every file/folder name `before` produced that `after` no longer
    // produces, so files named the old way can be found and fixed later.
    void recordOldNames(const Account& before, const Account& after,
                        const Database::NumberCorrections& corrections = {})
    {
        // The number files with an old name should use from now on: the corrected
        // text if this edit fixed a typo in it, otherwise the same number (the
        // name fixer falls back to the current number if the account no longer has it).
        auto targetNumber = [&](const std::string& n) {
            for (const auto& [from, to] : corrections)
                if (caseFoldKey(pathFromUtf8(from)) == caseFoldKey(pathFromUtf8(n))) return to;
            return n;
        };

        auto fileNames = [](const Account& a) {
            std::vector<std::pair<std::string, std::string>> out{{accountLabel(a), a.lastFour}};
            for (const auto& n : a.previousLastFour) out.push_back({accountLabel(a, n), n});
            return out;
        };
        std::set<std::string> current;
        for (const auto& [name, number] : fileNames(after)) current.insert(caseFoldKey(pathFromUtf8(name)));

        SQLite::Statement ins(db,
            "INSERT OR IGNORE INTO account_name_history (account_id, is_folder, name, last_four)"
            " VALUES (?, ?, ?, ?)");
        auto record = [&](bool folder, const std::string& name, const std::string& number) {
            ins.bind(1, before.id);
            ins.bind(2, folder ? 1 : 0);
            ins.bind(3, name);
            ins.bind(4, number);
            ins.exec();
            ins.reset();
        };

        for (const auto& [name, number] : fileNames(before))
            if (!current.count(caseFoldKey(pathFromUtf8(name)))) record(false, name, targetNumber(number));

        const std::string oldFolder = accountFolderLabel(before);
        if (caseFoldKey(pathFromUtf8(oldFolder)) != caseFoldKey(pathFromUtf8(accountFolderLabel(after))))
            record(true, oldFolder, {});
    }

    static AccountRecord readAccountRow(SQLite::Statement& q)
    {
        AccountRecord a;
        a.id = q.getColumn(0).getInt64();
        a.caseId = q.getColumn(1).getInt64();
        a.institution = q.getColumn(2).getString();
        a.accountType = q.getColumn(3).getString();
        a.lastFour = q.getColumn(4).getString();
        a.institutionDisplay = q.getColumn(5).getString();
        return a;
    }
};

// ---------------------------------------------------------------------------

Database::Database(const fs::path& file)
    : impl_(std::make_unique<Impl>(utf8FromPath(file)))  // SQLite wants UTF-8 file names
{
}

Database::Database(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Database Database::openInMemory()
{
    return Database(std::make_unique<Impl>(":memory:"));
}

Database::~Database() = default;
Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

int Database::schemaVersion() const { return impl_->userVersion(); }

// ---- Cases ----------------------------------------------------------------

std::vector<NewPerson> defaultCasePeople()
{
    return {{"Husband", "H"}, {"Wife", "W"}, {"Joint", "J"}};
}

std::int64_t Database::createCase(const ClientCase& c, const std::vector<NewPerson>& initialPeople)
{
    const std::string name = trimmed(c.clientName);
    if (name.empty()) throw DatabaseError("Client name is required.");

    SQLite::Transaction tx(impl_->db);
    SQLite::Statement q(impl_->db, "INSERT INTO cases (client_name, notes) VALUES (?, ?)");
    q.bind(1, name);
    q.bind(2, c.notes);
    q.exec();
    const std::int64_t id = impl_->db.getLastInsertRowid();

    for (const NewPerson& p : initialPeople) impl_->insertPerson(id, p.fullName, p.displayName);
    tx.commit();
    return id;
}

void Database::updateCase(const ClientCase& c)
{
    const std::string name = trimmed(c.clientName);
    if (name.empty()) throw DatabaseError("Client name is required.");

    SQLite::Statement q(impl_->db, "UPDATE cases SET client_name = ?, notes = ? WHERE id = ?");
    q.bind(1, name);
    q.bind(2, c.notes);
    q.bind(3, c.id);
    if (q.exec() == 0) throw DatabaseError("That case no longer exists.");
}

void Database::deleteCase(std::int64_t caseId)
{
    // Owner links point at people with RESTRICT, so remove the case's accounts
    // (and with them their owner links) before the cascade reaches people.
    SQLite::Transaction tx(impl_->db);
    SQLite::Statement delAccounts(impl_->db, "DELETE FROM accounts WHERE case_id = ?");
    delAccounts.bind(1, caseId);
    delAccounts.exec();

    SQLite::Statement delCase(impl_->db, "DELETE FROM cases WHERE id = ?");
    delCase.bind(1, caseId);
    delCase.exec();
    tx.commit();
}

std::optional<ClientCase> Database::getCase(std::int64_t caseId) const
{
    SQLite::Statement q(impl_->db, "SELECT id, client_name, notes FROM cases WHERE id = ?");
    q.bind(1, caseId);
    if (!q.executeStep()) return std::nullopt;
    return ClientCase{q.getColumn(0).getInt64(), q.getColumn(1).getString(),
                      q.getColumn(2).getString()};
}

std::vector<ClientCase> Database::listCases() const
{
    SQLite::Statement q(impl_->db,
        "SELECT id, client_name, notes FROM cases ORDER BY client_name COLLATE NOCASE, id");
    std::vector<ClientCase> out;
    while (q.executeStep())
        out.push_back({q.getColumn(0).getInt64(), q.getColumn(1).getString(),
                       q.getColumn(2).getString()});
    return out;
}

// ---- People ---------------------------------------------------------------

std::int64_t Database::addPerson(std::int64_t caseId, const std::string& fullName,
                                 const std::string& displayName)
{
    impl_->requireCase(caseId);
    return impl_->insertPerson(caseId, fullName, displayName);
}

void Database::updatePerson(std::int64_t personId, const std::string& fullName,
                            const std::string& displayName)
{
    auto [full, shown] = Impl::cleanNames(fullName, displayName);

    SQLite::Statement find(impl_->db, "SELECT case_id FROM people WHERE id = ?");
    find.bind(1, personId);
    if (!find.executeStep()) throw DatabaseError("That person no longer exists.");
    impl_->requireUniquePerson(find.getColumn(0).getInt64(), full, shown, personId);

    SQLite::Transaction tx(impl_->db);

    // A new display name changes the names of every account this person owns.
    std::vector<Account> before;
    SQLite::Statement owned(impl_->db, "SELECT account_id FROM account_owners WHERE person_id = ?");
    owned.bind(1, personId);
    while (owned.executeStep())
        if (auto acct = impl_->loadAccount(owned.getColumn(0).getInt64())) before.push_back(*acct);

    SQLite::Statement q(impl_->db, "UPDATE people SET full_name = ?, display_name = ? WHERE id = ?");
    q.bind(1, full);
    q.bind(2, shown);
    q.bind(3, personId);
    q.exec();

    for (const Account& old : before) impl_->recordOldNames(old, *impl_->loadAccount(old.id));
    tx.commit();
}

void Database::deletePerson(std::int64_t personId)
{
    SQLite::Statement owned(impl_->db, R"sql(
        SELECT a.institution, a.account_type, a.last_four
        FROM account_owners o JOIN accounts a ON a.id = o.account_id
        WHERE o.person_id = ?
        ORDER BY a.institution, a.account_type, a.last_four)sql");
    owned.bind(1, personId);

    std::string accounts;
    while (owned.executeStep()) {
        if (!accounts.empty()) accounts += ", ";
        accounts += owned.getColumn(0).getString() + ' ' + owned.getColumn(1).getString() + ' ' +
                    owned.getColumn(2).getString();
    }
    if (!accounts.empty())
        throw DatabaseError("This person is an owner of: " + accounts +
                            ". Remove them from those accounts first.");

    SQLite::Statement q(impl_->db, "DELETE FROM people WHERE id = ?");
    q.bind(1, personId);
    q.exec();
}

std::vector<Person> Database::listPeople(std::int64_t caseId) const
{
    SQLite::Statement q(impl_->db,
        "SELECT id, case_id, full_name, display_name FROM people WHERE case_id = ? ORDER BY id");
    q.bind(1, caseId);
    std::vector<Person> out;
    while (q.executeStep())
        out.push_back({q.getColumn(0).getInt64(), q.getColumn(1).getInt64(),
                       q.getColumn(2).getString(), q.getColumn(3).getString()});
    return out;
}

// ---- Accounts -------------------------------------------------------------

std::int64_t Database::createAccount(const AccountRecord& a)
{
    impl_->requireCase(a.caseId);
    impl_->validate(a);

    SQLite::Transaction tx(impl_->db);
    SQLite::Statement q(impl_->db,
        "INSERT INTO accounts (case_id, institution, account_type, last_four, institution_display)"
        " VALUES (?, ?, ?, ?, ?)");
    q.bind(1, a.caseId);
    q.bind(2, trimmed(a.institution));
    q.bind(3, trimmed(a.accountType));
    q.bind(4, trimmed(a.lastFour));
    q.bind(5, institutionDisplayOrDefault(a));
    q.exec();
    const std::int64_t id = impl_->db.getLastInsertRowid();
    impl_->writeOwners(id, a.ownerIds);
    impl_->writePreviousNumbers(id, a.previousLastFour);
    tx.commit();
    return id;
}

void Database::updateAccount(const AccountRecord& a, const NumberCorrections& numberCorrections)
{
    const auto existing = getAccount(a.id);
    if (!existing) throw DatabaseError("That account no longer exists.");

    AccountRecord checked = a;
    checked.caseId = existing->caseId;  // accounts never move between cases
    impl_->validate(checked);

    SQLite::Transaction tx(impl_->db);
    const std::optional<Account> before = impl_->loadAccount(a.id);
    SQLite::Statement q(impl_->db,
        "UPDATE accounts SET institution = ?, account_type = ?, last_four = ?, institution_display = ?"
        " WHERE id = ?");
    q.bind(1, trimmed(a.institution));
    q.bind(2, trimmed(a.accountType));
    q.bind(3, trimmed(a.lastFour));
    q.bind(4, institutionDisplayOrDefault(a));
    q.bind(5, a.id);
    q.exec();
    impl_->writeOwners(a.id, a.ownerIds);
    impl_->writePreviousNumbers(a.id, a.previousLastFour);
    if (before) impl_->recordOldNames(*before, *impl_->loadAccount(a.id), numberCorrections);
    tx.commit();
}

void Database::deleteAccount(std::int64_t accountId)
{
    SQLite::Statement q(impl_->db, "DELETE FROM accounts WHERE id = ?");
    q.bind(1, accountId);
    q.exec();
}

std::optional<AccountRecord> Database::getAccount(std::int64_t accountId) const
{
    SQLite::Statement q(impl_->db,
        "SELECT id, case_id, institution, account_type, last_four, institution_display"
        " FROM accounts WHERE id = ?");
    q.bind(1, accountId);
    if (!q.executeStep()) return std::nullopt;
    AccountRecord a = Impl::readAccountRow(q);
    a.ownerIds = impl_->ownersOf(a.id);
    a.previousLastFour = impl_->previousOf(a.id);
    return a;
}

std::vector<AccountRecord> Database::listAccountRecords(std::int64_t caseId) const
{
    SQLite::Statement q(impl_->db, R"sql(
        SELECT id, case_id, institution, account_type, last_four, institution_display FROM accounts
        WHERE case_id = ?
        ORDER BY institution COLLATE NOCASE, account_type COLLATE NOCASE, last_four, id)sql");
    q.bind(1, caseId);
    std::vector<AccountRecord> out;
    while (q.executeStep()) out.push_back(Impl::readAccountRow(q));
    for (auto& a : out) {
        a.ownerIds = impl_->ownersOf(a.id);
        a.previousLastFour = impl_->previousOf(a.id);
    }
    return out;
}

std::vector<Account> Database::loadAccounts(std::int64_t caseId) const
{
    std::vector<Account> out;
    for (const AccountRecord& r : listAccountRecords(caseId)) out.push_back(impl_->toAccount(r));
    return out;
}

std::vector<OldAccountName> Database::oldAccountNames(std::int64_t caseId) const
{
    SQLite::Statement q(impl_->db, R"sql(
        SELECT h.account_id, h.is_folder, h.name, h.last_four
        FROM account_name_history h JOIN accounts a ON a.id = h.account_id
        WHERE a.case_id = ?
        ORDER BY h.recorded_at, h.rowid)sql");
    q.bind(1, caseId);
    std::vector<OldAccountName> out;
    while (q.executeStep())
        out.push_back({q.getColumn(0).getInt64(), q.getColumn(1).getInt() != 0,
                       q.getColumn(2).getString(), q.getColumn(3).getString()});
    return out;
}

std::vector<std::string> Database::accountTypeSuggestions() const
{
    SQLite::Statement q(impl_->db, R"sql(
        SELECT MIN(account_type) FROM accounts
        GROUP BY account_type COLLATE NOCASE
        ORDER BY 1 COLLATE NOCASE)sql");
    std::vector<std::string> out;
    while (q.executeStep()) out.push_back(q.getColumn(0).getString());
    return out;
}

std::vector<InstitutionName> Database::institutionSuggestions() const
{
    // One row per institution (ignoring letter case): the most recently added account's.
    SQLite::Statement q(impl_->db, R"sql(
        SELECT a.institution, a.institution_display FROM accounts a
        WHERE a.id = (SELECT MAX(b.id) FROM accounts b
                      WHERE b.institution = a.institution COLLATE NOCASE)
        ORDER BY a.institution COLLATE NOCASE)sql");
    std::vector<InstitutionName> out;
    while (q.executeStep())
        out.push_back({q.getColumn(0).getString(), q.getColumn(1).getString()});
    return out;
}

// ---- Rename history -------------------------------------------------------

void Database::saveBatch(const BatchRecord& batch, std::optional<std::int64_t> caseId)
{
    SQLite::Transaction tx(impl_->db);

    SQLite::Statement b(impl_->db,
        "INSERT INTO rename_batches (batch_id, case_id, root) VALUES (?, ?, ?)");
    b.bind(1, batch.batchId);
    if (caseId) b.bind(2, *caseId);
    else b.bind(2);  // NULL
    b.bind(3, utf8FromPath(batch.root));
    b.exec();

    SQLite::Statement m(impl_->db,
        "INSERT INTO rename_log (batch_id, seq, from_path, to_path) VALUES (?, ?, ?, ?)");
    int seq = 0;
    for (const ExecutedMove& move : batch.moves) {
        m.bind(1, batch.batchId);
        m.bind(2, seq++);
        m.bind(3, utf8FromPath(move.from));
        m.bind(4, utf8FromPath(move.to));
        m.exec();
        m.reset();
    }

    SQLite::Statement f(impl_->db,
        "INSERT INTO created_folders (batch_id, seq, path) VALUES (?, ?, ?)");
    seq = 0;
    for (const fs::path& folder : batch.createdFolders) {
        f.bind(1, batch.batchId);
        f.bind(2, seq++);
        f.bind(3, utf8FromPath(folder));
        f.exec();
        f.reset();
    }

    tx.commit();
}

std::optional<BatchRecord> Database::loadBatch(const std::string& batchId) const
{
    SQLite::Statement b(impl_->db, "SELECT root FROM rename_batches WHERE batch_id = ?");
    b.bind(1, batchId);
    if (!b.executeStep()) return std::nullopt;

    BatchRecord record;
    record.batchId = batchId;
    record.root = pathFromUtf8(b.getColumn(0).getString());

    SQLite::Statement m(impl_->db,
        "SELECT from_path, to_path FROM rename_log WHERE batch_id = ? ORDER BY seq");
    m.bind(1, batchId);
    while (m.executeStep())
        record.moves.push_back({pathFromUtf8(m.getColumn(0).getString()),
                                pathFromUtf8(m.getColumn(1).getString())});

    SQLite::Statement f(impl_->db,
        "SELECT path FROM created_folders WHERE batch_id = ? ORDER BY seq");
    f.bind(1, batchId);
    while (f.executeStep()) record.createdFolders.push_back(pathFromUtf8(f.getColumn(0).getString()));

    return record;
}

std::vector<BatchSummary> Database::listBatches(std::size_t limit) const
{
    SQLite::Statement q(impl_->db, R"sql(
        SELECT b.batch_id, b.case_id, b.root, b.created_at, b.undone,
               (SELECT COUNT(*) FROM rename_log l WHERE l.batch_id = b.batch_id)
        FROM rename_batches b
        ORDER BY b.created_at DESC, b.rowid DESC
        LIMIT ?)sql");
    q.bind(1, static_cast<std::int64_t>(limit));

    std::vector<BatchSummary> out;
    while (q.executeStep()) {
        BatchSummary s;
        s.batchId = q.getColumn(0).getString();
        if (!q.getColumn(1).isNull()) s.caseId = q.getColumn(1).getInt64();
        s.root = pathFromUtf8(q.getColumn(2).getString());
        s.createdAt = q.getColumn(3).getString();
        s.undone = q.getColumn(4).getInt() != 0;
        s.moveCount = static_cast<std::size_t>(q.getColumn(5).getInt64());
        out.push_back(std::move(s));
    }
    return out;
}

void Database::markBatchUndone(const std::string& batchId)
{
    SQLite::Statement q(impl_->db, "UPDATE rename_batches SET undone = 1 WHERE batch_id = ?");
    q.bind(1, batchId);
    if (q.exec() == 0) throw DatabaseError("That rename batch no longer exists.");
}

}  // namespace finrenamer

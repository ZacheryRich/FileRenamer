#include "finrenamer/Database.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <cstdio>
#include <set>

#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {

namespace {

constexpr int kSchemaVersion = 14;

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

    // Version 5: combined statements -- an accounts row (is_combined = 1) that
    // stands for one PDF covering several accounts, listed in name order.
    R"sql(
    ALTER TABLE accounts ADD COLUMN is_combined INTEGER NOT NULL DEFAULT 0;

    CREATE TABLE combined_members (
        combined_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        position    INTEGER NOT NULL,
        PRIMARY KEY (combined_id, account_id)
    );
    CREATE INDEX idx_combined_members_account ON combined_members(account_id);
    )sql",

    // Version 6: optional opening and closing dates ("YYYY-MM-DD", '' = unknown).
    R"sql(
    ALTER TABLE accounts ADD COLUMN opened_on TEXT NOT NULL DEFAULT '';
    ALTER TABLE accounts ADD COLUMN closed_on TEXT NOT NULL DEFAULT '';
    )sql",

    // Version 7: a combined statement has its own institution and a typed list of
    // (type, number) parts instead of links to other accounts. Existing statements
    // keep what their linked accounts showed.
    R"sql(
    CREATE TABLE combined_parts (
        combined_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        position     INTEGER NOT NULL,
        account_type TEXT NOT NULL,
        last_four    TEXT NOT NULL,
        PRIMARY KEY (combined_id, position)
    );

    INSERT INTO combined_parts (combined_id, position, account_type, last_four)
        SELECT cm.combined_id, cm.position, m.account_type, m.last_four
        FROM combined_members cm JOIN accounts m ON m.id = cm.account_id;

    UPDATE accounts SET
        institution = COALESCE((SELECT m.institution FROM combined_members cm
            JOIN accounts m ON m.id = cm.account_id
            WHERE cm.combined_id = accounts.id ORDER BY cm.position LIMIT 1), institution),
        institution_display = COALESCE((SELECT m.institution_display FROM combined_members cm
            JOIN accounts m ON m.id = cm.account_id
            WHERE cm.combined_id = accounts.id ORDER BY cm.position LIMIT 1), institution_display)
        WHERE is_combined = 1;

    DROP TABLE combined_members;
    )sql",

    // Version 8: user-defined file name templates for the rename screen (emails, texts...).
    R"sql(
    CREATE TABLE document_templates (
        id           INTEGER PRIMARY KEY,
        name         TEXT NOT NULL,
        pattern      TEXT NOT NULL,
        allow_period INTEGER NOT NULL DEFAULT 0
    );
    )sql",

    // Version 9: a template can leave the date out of the name.
    R"sql(
    ALTER TABLE document_templates ADD COLUMN use_date INTEGER NOT NULL DEFAULT 1;
    )sql",

    // Version 10: a template can attach its text straight to the date (no space between).
    R"sql(
    ALTER TABLE document_templates ADD COLUMN space_after_date INTEGER NOT NULL DEFAULT 1;
    )sql",
    // Version 11: each account on a combined statement has its own owners (the name groups
    // accounts by owner). Existing statements give every part the statement's owners, so their
    // names stay as they were. account_owners keeps the union of the parts' owners, so deleting
    // or renaming a person still finds the statement.
    R"sql(
    CREATE TABLE combined_part_owners (
        combined_id   INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        part_position INTEGER NOT NULL,
        person_id     INTEGER NOT NULL REFERENCES people(id) ON DELETE CASCADE,
        position      INTEGER NOT NULL,
        PRIMARY KEY (combined_id, part_position, person_id)
    );

    INSERT INTO combined_part_owners (combined_id, part_position, person_id, position)
        SELECT cp.combined_id, cp.position, ao.person_id, ao.position
        FROM combined_parts cp JOIN account_owners ao ON ao.account_id = cp.combined_id;
    )sql",
    // v12: the attorney directory.
    R"sql(
    CREATE TABLE firms (
        id       INTEGER PRIMARY KEY,
        name     TEXT NOT NULL COLLATE NOCASE UNIQUE,
        phone    TEXT NOT NULL DEFAULT '',
        fax      TEXT NOT NULL DEFAULT '',
        address  TEXT NOT NULL DEFAULT '',
        is_ours  INTEGER NOT NULL DEFAULT 0
    );
    CREATE TABLE attorneys (
        id         INTEGER PRIMARY KEY,
        firm_id    INTEGER NOT NULL REFERENCES firms(id),
        name       TEXT NOT NULL,
        bar_number TEXT NOT NULL DEFAULT '',
        email      TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX attorneys_by_firm ON attorneys(firm_id);
    )sql",
    // v13: court details, parties and attorneys on a case.
    R"sql(
    ALTER TABLE cases ADD COLUMN cause_number TEXT NOT NULL DEFAULT '';
    ALTER TABLE cases ADD COLUMN county TEXT NOT NULL DEFAULT '';
    ALTER TABLE cases ADD COLUMN court TEXT NOT NULL DEFAULT '';
    CREATE TABLE case_parties (
        id       INTEGER PRIMARY KEY,
        case_id  INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
        name     TEXT NOT NULL,
        role     TEXT NOT NULL CHECK (role IN ('Petitioner', 'Respondent')),
        position INTEGER NOT NULL
    );
    CREATE TABLE case_attorneys (
        case_id     INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
        attorney_id INTEGER NOT NULL REFERENCES attorneys(id) ON DELETE CASCADE,
        PRIMARY KEY (case_id, attorney_id)
    );
    )sql",
    // v14: case information for documents (all optional): more case fields, party details,
    // children, other people, and each attorney's letter sign-off.
    R"sql(
    ALTER TABLE cases ADD COLUMN case_type TEXT NOT NULL DEFAULT '';
    ALTER TABLE cases ADD COLUMN marriage_date TEXT NOT NULL DEFAULT '';
    ALTER TABLE cases ADD COLUMN marriage_place TEXT NOT NULL DEFAULT '';
    ALTER TABLE cases ADD COLUMN separation_date TEXT NOT NULL DEFAULT '';
    ALTER TABLE cases ADD COLUMN insurance TEXT NOT NULL DEFAULT '';
    ALTER TABLE cases ADD COLUMN insurance_carrier TEXT NOT NULL DEFAULT '';
    ALTER TABLE cases ADD COLUMN policy_number TEXT NOT NULL DEFAULT '';
    ALTER TABLE cases ADD COLUMN group_number TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN is_client INTEGER NOT NULL DEFAULT 0;
    ALTER TABLE case_parties ADD COLUMN address TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN city_state_zip TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN phone TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN dob TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN pob TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN ssn TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN drivers_license TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN car TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN employer TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN email TEXT NOT NULL DEFAULT '';
    ALTER TABLE case_parties ADD COLUMN name_change TEXT NOT NULL DEFAULT '';
    CREATE TABLE case_children (
        id       INTEGER PRIMARY KEY,
        case_id  INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
        position INTEGER NOT NULL,
        name TEXT NOT NULL DEFAULT '', age TEXT NOT NULL DEFAULT '', sex TEXT NOT NULL DEFAULT '',
        dob TEXT NOT NULL DEFAULT '', pob TEXT NOT NULL DEFAULT '', school TEXT NOT NULL DEFAULT '',
        ssn TEXT NOT NULL DEFAULT ''
    );
    CREATE TABLE case_contacts (
        id       INTEGER PRIMARY KEY,
        case_id  INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
        position INTEGER NOT NULL,
        role TEXT NOT NULL DEFAULT '', name TEXT NOT NULL DEFAULT '', firm TEXT NOT NULL DEFAULT '',
        bar_number TEXT NOT NULL DEFAULT '', address TEXT NOT NULL DEFAULT '', phone TEXT NOT NULL DEFAULT '',
        fax TEXT NOT NULL DEFAULT '', email TEXT NOT NULL DEFAULT ''
    );
    ALTER TABLE attorneys ADD COLUMN sign_off TEXT NOT NULL DEFAULT '';
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

// Columns read by readAccountRow(), in order.
constexpr const char* kAccountColumns =
    "id, case_id, institution, account_type, last_four, institution_display, is_combined,"
    " opened_on, closed_on";

std::string foldKey(const std::string& s) { return caseFoldKey(pathFromUtf8(s)); }

// Dates are stored as "YYYY-MM-DD"; '' means not set.
std::string dateToText(const std::optional<std::chrono::year_month_day>& d)
{
    if (!d) return {};
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(d->year()),
                  static_cast<unsigned>(d->month()), static_cast<unsigned>(d->day()));
    return buf;
}

std::optional<std::chrono::year_month_day> textToDate(const std::string& text)
{
    int y = 0;
    unsigned m = 0, d = 0;
    if (std::sscanf(text.c_str(), "%4d-%2u-%2u", &y, &m, &d) != 3) return std::nullopt;
    const auto date = makeDate(y, m, d);
    if (!date.ok()) return std::nullopt;
    return date;
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
        const int current = userVersion();
        // An existing database that is about to be upgraded is copied first, so a bad
        // upgrade can't cost the only copy. (Fresh and in-memory databases have nothing to lose.)
        if (current > 0 && current < kSchemaVersion && utf8File != ":memory:") {
            const fs::path file = pathFromUtf8(utf8File);
            fs::path copy = file;
            copy += ".backup-v" + std::to_string(current);
            for (int n = 2; fs::exists(copy); ++n) {
                copy = file;
                copy += ".backup-v" + std::to_string(current) + "-" + std::to_string(n);
            }
            try {
                copyTo(copy);
            } catch (const std::exception& e) {
                throw DatabaseError("The database needs upgrading, but a backup copy could not be made first, "
                                    "so nothing was changed. " + std::string(e.what()));
            }
        }
        migrate();
    }

    // A consistent snapshot of the whole database in `dest` (replacing it if it exists).
    void copyTo(const fs::path& dest)
    {
        fs::path temp = dest;
        temp += ".writing";
        std::error_code ignore;
        fs::remove(temp, ignore);
        std::string quoted = utf8FromPath(temp);
        for (std::size_t at = quoted.find('\''); at != std::string::npos; at = quoted.find('\'', at + 2))
            quoted.insert(at, "'");
        db.exec("VACUUM INTO '" + quoted + "'");
        fs::rename(temp, dest);  // replaces an existing file
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

    void validateOwners(const AccountRecord& a)
    {
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

    void validateCombined(const AccountRecord& a)
    {
        if (a.parts.size() < 2) throw DatabaseError("A combined statement needs at least two accounts.");
        if (trimmed(a.institution).empty()) throw DatabaseError("A combined statement needs an institution.");
        std::set<std::string> seen;
        for (const CombinedPart& p : a.parts) {
            const std::string number = trimmed(p.lastFour);
            if (number.empty()) throw DatabaseError("An account on the combined statement has no number.");
            const std::string key = caseFoldKey(pathFromUtf8(trimmed(p.accountType) + "|" + number));
            if (!seen.insert(key).second)
                throw DatabaseError("The same account (" + trimmed(p.accountType) + " " + number +
                                    ") is listed twice.");
        }
        // Each account's owners: real people of this case, none twice.
        AccountRecord each;
        each.caseId = a.caseId;
        for (const CombinedPart& p : a.parts) {
            each.ownerIds = p.ownerIds;
            validateOwners(each);
        }
    }

    void validate(const AccountRecord& a)
    {
        if (a.isCombined()) return validateCombined(a);

        for (const auto& d : {a.openedOn, a.closedOn})
            if (d && !d->ok()) throw DatabaseError("An opening or closing date isn't a real date.");
        if (a.openedOn && a.closedOn && *a.closedOn < *a.openedOn)
            throw DatabaseError("The account is closed before it was opened. Check the Opened on and Closed on dates.");

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
        validateOwners(a);
    }

    void writeParts(std::int64_t combinedId, const std::vector<CombinedPart>& parts)
    {
        SQLite::Statement del(db, "DELETE FROM combined_parts WHERE combined_id = ?");
        del.bind(1, combinedId);
        del.exec();

        SQLite::Statement ins(db,
            "INSERT INTO combined_parts (combined_id, position, account_type, last_four) VALUES (?, ?, ?, ?)");
        int position = 0;
        for (const CombinedPart& p : parts) {
            ins.bind(1, combinedId);
            ins.bind(2, position++);
            ins.bind(3, trimmed(p.accountType));
            ins.bind(4, trimmed(p.lastFour));
            ins.exec();
            ins.reset();
        }

        SQLite::Statement delOwners(db, "DELETE FROM combined_part_owners WHERE combined_id = ?");
        delOwners.bind(1, combinedId);
        delOwners.exec();
        SQLite::Statement insOwner(db,
            "INSERT INTO combined_part_owners (combined_id, part_position, person_id, position) VALUES (?, ?, ?, ?)");
        int partPosition = 0;
        for (const CombinedPart& p : parts) {
            int ownerPosition = 0;
            for (const auto personId : p.ownerIds) {
                insOwner.bind(1, combinedId);
                insOwner.bind(2, partPosition);
                insOwner.bind(3, personId);
                insOwner.bind(4, ownerPosition++);
                insOwner.exec();
                insOwner.reset();
            }
            ++partPosition;
        }
    }

    // A combined statement given only statement-level owners (AccountRecord::ownerIds, no
    // per-account owners) gets them on every account: the shorthand the older code used.
    static AccountRecord withPartOwners(const AccountRecord& a)
    {
        AccountRecord out = a;
        if (!out.isCombined() || out.ownerIds.empty()) return out;
        for (const CombinedPart& p : out.parts)
            if (!p.ownerIds.empty()) return out;
        for (CombinedPart& p : out.parts) p.ownerIds = out.ownerIds;
        return out;
    }

    // Everyone on a combined statement, once, in order of first appearance.
    static std::vector<std::int64_t> unionOwnerIds(const std::vector<CombinedPart>& parts)
    {
        std::vector<std::int64_t> out;
        for (const CombinedPart& p : parts)
            for (const auto id : p.ownerIds)
                if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
        return out;
    }

    std::vector<CombinedPart> partsOf(std::int64_t combinedId)
    {
        SQLite::Statement q(db,
            "SELECT account_type, last_four FROM combined_parts WHERE combined_id = ? ORDER BY position");
        q.bind(1, combinedId);
        std::vector<CombinedPart> out;
        while (q.executeStep()) out.push_back({q.getColumn(0).getString(), q.getColumn(1).getString(), {}, {}});

        SQLite::Statement owners(db,
            "SELECT part_position, person_id FROM combined_part_owners WHERE combined_id = ?"
            " ORDER BY part_position, position");
        owners.bind(1, combinedId);
        while (owners.executeStep()) {
            const auto position = static_cast<std::size_t>(owners.getColumn(0).getInt());
            if (position < out.size()) out[position].ownerIds.push_back(owners.getColumn(1).getInt64());
        }
        return out;
    }

    // Owners, previous numbers and (for a combined statement) its parts.
    void completeRecord(AccountRecord& a, bool combined)
    {
        a.ownerIds = ownersOf(a.id);
        a.previousLastFour = previousOf(a.id);
        if (combined) a.parts = partsOf(a.id);
    }

    std::optional<AccountRecord> getRecord(std::int64_t accountId)
    {
        SQLite::Statement q(db, std::string("SELECT ") + kAccountColumns + " FROM accounts WHERE id = ?");
        q.bind(1, accountId);
        if (!q.executeStep()) return std::nullopt;
        AccountRecord a = readAccountRow(q);
        completeRecord(a, q.getColumn(6).getInt() != 0);
        return a;
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
        a.openedOn = r.openedOn;
        a.closedOn = r.closedOn;
        a.combined = r.parts;
        for (CombinedPart& part : a.combined) {
            part.owners.clear();
            for (const auto personId : part.ownerIds) {
                SQLite::Statement who(db, "SELECT display_name FROM people WHERE id = ?");
                who.bind(1, personId);
                if (who.executeStep()) part.owners.push_back(who.getColumn(0).getString());
            }
        }

        SQLite::Statement names(db, R"sql(
            SELECT p.display_name FROM account_owners o JOIN people p ON p.id = o.person_id
            WHERE o.account_id = ? ORDER BY o.position)sql");
        names.bind(1, r.id);
        while (names.executeStep()) a.owners.push_back(names.getColumn(0).getString());
        return a;
    }

    std::optional<Account> loadAccount(std::int64_t accountId)
    {
        const auto r = getRecord(accountId);
        if (!r) return std::nullopt;
        return toAccount(*r);
    }

    std::vector<Account> loadAccounts(const std::vector<std::int64_t>& ids)
    {
        std::vector<Account> out;
        for (const auto id : ids)
            if (auto a = loadAccount(id)) out.push_back(std::move(*a));
        return out;
    }

    // Remembers every file/folder name `before` produced that `after` no longer
    // produces, so files named the old way can be found and fixed later.
    void recordOldNames(const Account& before, const Account& after,
                        const Database::NumberCorrections& corrections = {}, bool folderOnly = false)
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

        if (!folderOnly)
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
        a.openedOn = textToDate(q.getColumn(7).getString());
        a.closedOn = textToDate(q.getColumn(8).getString());
        return a;
    }
};

// ---------------------------------------------------------------------------

Database::Database(const fs::path& file)
    : impl_(std::make_unique<Impl>(utf8FromPath(file)))  // SQLite wants UTF-8 file names
{
}

void Database::backupTo(const fs::path& file) const
{
    try {
        impl_->copyTo(file);
    } catch (const std::exception& e) {
        throw DatabaseError(std::string("The backup could not be written: ") + e.what());
    }
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

namespace {

const char* kCaseColumns =
    "id, client_name, notes, cause_number, county, court, case_type, marriage_date, marriage_place,"
    " separation_date, insurance, insurance_carrier, policy_number, group_number";

ClientCase readCase(SQLite::Statement& q)
{
    ClientCase c;
    c.id = q.getColumn(0).getInt64();
    std::string* text[] = {&c.clientName, &c.notes, &c.causeNumber, &c.county, &c.court, &c.caseType,
                           &c.marriageDate, &c.marriagePlace, &c.separationDate, &c.insurance,
                           &c.insuranceCarrier, &c.policyNumber, &c.groupNumber};
    for (int i = 0; i < 13; ++i) *text[i] = q.getColumn(i + 1).getString();
    return c;
}

// Binds the case's text columns (everything after the id) starting at `first`.
void bindCase(SQLite::Statement& q, const ClientCase& c, int first)
{
    const std::string text[] = {c.clientName, c.notes, c.causeNumber, c.county, c.court, c.caseType,
                                c.marriageDate, c.marriagePlace, c.separationDate, c.insurance,
                                c.insuranceCarrier, c.policyNumber, c.groupNumber};
    for (int i = 0; i < 13; ++i) q.bind(first + i, i == 1 ? text[i] : trimmed(text[i]));  // notes kept as typed
}

}  // namespace

std::int64_t Database::createCase(const ClientCase& c, const std::vector<NewPerson>& initialPeople)
{
    // Every case field is optional: a case can be saved with whatever is known so far.
    SQLite::Transaction tx(impl_->db);
    SQLite::Statement q(impl_->db,
        "INSERT INTO cases (client_name, notes, cause_number, county, court, case_type, marriage_date,"
        " marriage_place, separation_date, insurance, insurance_carrier, policy_number, group_number)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    bindCase(q, c, 1);
    q.exec();
    const std::int64_t id = impl_->db.getLastInsertRowid();

    for (const NewPerson& p : initialPeople) impl_->insertPerson(id, p.fullName, p.displayName);
    tx.commit();
    return id;
}

void Database::updateCase(const ClientCase& c)
{
    SQLite::Statement q(impl_->db,
        "UPDATE cases SET client_name = ?, notes = ?, cause_number = ?, county = ?, court = ?, case_type = ?,"
        " marriage_date = ?, marriage_place = ?, separation_date = ?, insurance = ?, insurance_carrier = ?,"
        " policy_number = ?, group_number = ? WHERE id = ?");
    bindCase(q, c, 1);
    q.bind(14, c.id);
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
    SQLite::Statement q(impl_->db, std::string("SELECT ") + kCaseColumns + " FROM cases WHERE id = ?");
    q.bind(1, caseId);
    if (!q.executeStep()) return std::nullopt;
    return readCase(q);
}

std::vector<ClientCase> Database::listCases() const
{
    SQLite::Statement q(impl_->db, std::string("SELECT ") + kCaseColumns +
                                       " FROM cases ORDER BY client_name = '', client_name COLLATE NOCASE,"
                                       " cause_number COLLATE NOCASE, id");
    std::vector<ClientCase> out;
    while (q.executeStep()) out.push_back(readCase(q));
    return out;
}

// ---- Parties -----------------------------------------------------------------

static PartyRole roleFromText(const std::string& t)
{
    return t == "Respondent" ? PartyRole::Respondent : PartyRole::Petitioner;
}

namespace {

const char* kPartyColumns =
    "id, case_id, name, role, is_client, address, city_state_zip, phone, dob, pob, ssn, drivers_license, car,"
    " employer, email, name_change";

CaseParty readParty(SQLite::Statement& q)
{
    CaseParty p;
    p.id = q.getColumn(0).getInt64();
    p.caseId = q.getColumn(1).getInt64();
    p.name = q.getColumn(2).getString();
    p.role = q.getColumn(3).getString() == "Respondent" ? PartyRole::Respondent : PartyRole::Petitioner;
    p.ourClient = q.getColumn(4).getInt() != 0;
    std::string* text[] = {&p.address, &p.cityStateZip, &p.phone, &p.dob, &p.pob, &p.ssn, &p.driversLicense,
                           &p.car, &p.employer, &p.email, &p.nameChange};
    for (int i = 0; i < 11; ++i) *text[i] = q.getColumn(i + 5).getString();
    return p;
}

// name, role, is_client and the 11 detail columns, from `first`.
void bindParty(SQLite::Statement& q, const CaseParty& p, int first)
{
    q.bind(first, trimmed(p.name));
    q.bind(first + 1, partyRoleName(p.role));
    q.bind(first + 2, p.ourClient ? 1 : 0);
    const std::string text[] = {p.address, p.cityStateZip, p.phone, p.dob, p.pob, p.ssn, p.driversLicense,
                                p.car, p.employer, p.email, p.nameChange};
    for (int i = 0; i < 11; ++i) q.bind(first + 3 + i, trimmed(text[i]));
}

}  // namespace

std::vector<CaseParty> Database::listParties(std::int64_t caseId) const
{
    SQLite::Statement q(impl_->db, std::string("SELECT ") + kPartyColumns +
                                       " FROM case_parties WHERE case_id = ? ORDER BY position, id");
    q.bind(1, caseId);
    std::vector<CaseParty> out;
    while (q.executeStep()) out.push_back(readParty(q));
    return out;
}

std::int64_t Database::addParty(std::int64_t caseId, const std::string& name, PartyRole role)
{
    CaseParty p;
    p.caseId = caseId;
    p.name = name;
    p.role = role;
    return addParty(p);
}

std::int64_t Database::addParty(const CaseParty& p)
{
    if (!getCase(p.caseId)) throw DatabaseError("That case no longer exists.");
    SQLite::Statement q(impl_->db,
        "INSERT INTO case_parties (name, role, is_client, address, city_state_zip, phone, dob, pob, ssn,"
        " drivers_license, car, employer, email, name_change, case_id, position)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,"
        " (SELECT COALESCE(MAX(position), -1) + 1 FROM case_parties WHERE case_id = ?))");
    bindParty(q, p, 1);
    q.bind(15, p.caseId);
    q.bind(16, p.caseId);
    q.exec();
    return impl_->db.getLastInsertRowid();
}

void Database::updateParty(const CaseParty& p)
{
    SQLite::Statement q(impl_->db,
        "UPDATE case_parties SET name = ?, role = ?, is_client = ?, address = ?, city_state_zip = ?, phone = ?,"
        " dob = ?, pob = ?, ssn = ?, drivers_license = ?, car = ?, employer = ?, email = ?, name_change = ?"
        " WHERE id = ?");
    bindParty(q, p, 1);
    q.bind(15, p.id);
    if (q.exec() == 0) throw DatabaseError("That party no longer exists.");
}

void Database::deleteParty(std::int64_t partyId)
{
    SQLite::Statement q(impl_->db, "DELETE FROM case_parties WHERE id = ?");
    q.bind(1, partyId);
    q.exec();
}

// ---- Children and other people ----------------------------------------------------

namespace {

CaseChild readChild(SQLite::Statement& q)
{
    CaseChild c;
    c.id = q.getColumn(0).getInt64();
    c.caseId = q.getColumn(1).getInt64();
    std::string* text[] = {&c.name, &c.age, &c.sex, &c.dob, &c.pob, &c.school, &c.ssn};
    for (int i = 0; i < 7; ++i) *text[i] = q.getColumn(i + 2).getString();
    return c;
}

void bindChild(SQLite::Statement& q, const CaseChild& c)
{
    const std::string text[] = {c.name, c.age, c.sex, c.dob, c.pob, c.school, c.ssn};
    for (int i = 0; i < 7; ++i) q.bind(i + 1, trimmed(text[i]));
}

CaseContact readContact(SQLite::Statement& q)
{
    CaseContact c;
    c.id = q.getColumn(0).getInt64();
    c.caseId = q.getColumn(1).getInt64();
    std::string* text[] = {&c.role, &c.name, &c.firm, &c.barNumber, &c.address, &c.phone, &c.fax, &c.email};
    for (int i = 0; i < 8; ++i) *text[i] = q.getColumn(i + 2).getString();
    return c;
}

void bindContact(SQLite::Statement& q, const CaseContact& c)
{
    const std::string text[] = {c.role, c.name, c.firm, c.barNumber, c.address, c.phone, c.fax, c.email};
    for (int i = 0; i < 8; ++i) q.bind(i + 1, trimmed(text[i]));
}

}  // namespace

std::vector<CaseChild> Database::listChildren(std::int64_t caseId) const
{
    SQLite::Statement q(impl_->db,
        "SELECT id, case_id, name, age, sex, dob, pob, school, ssn FROM case_children WHERE case_id = ?"
        " ORDER BY position, id");
    q.bind(1, caseId);
    std::vector<CaseChild> out;
    while (q.executeStep()) out.push_back(readChild(q));
    return out;
}

std::int64_t Database::addChild(const CaseChild& c)
{
    if (!getCase(c.caseId)) throw DatabaseError("That case no longer exists.");
    SQLite::Statement q(impl_->db,
        "INSERT INTO case_children (name, age, sex, dob, pob, school, ssn, case_id, position)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, (SELECT COALESCE(MAX(position), -1) + 1 FROM case_children WHERE case_id = ?))");
    bindChild(q, c);
    q.bind(8, c.caseId);
    q.bind(9, c.caseId);
    q.exec();
    return impl_->db.getLastInsertRowid();
}

void Database::updateChild(const CaseChild& c)
{
    SQLite::Statement q(impl_->db,
        "UPDATE case_children SET name = ?, age = ?, sex = ?, dob = ?, pob = ?, school = ?, ssn = ? WHERE id = ?");
    bindChild(q, c);
    q.bind(8, c.id);
    if (q.exec() == 0) throw DatabaseError("That child no longer exists.");
}

void Database::deleteChild(std::int64_t childId)
{
    SQLite::Statement q(impl_->db, "DELETE FROM case_children WHERE id = ?");
    q.bind(1, childId);
    q.exec();
}

std::vector<CaseContact> Database::listContacts(std::int64_t caseId) const
{
    SQLite::Statement q(impl_->db,
        "SELECT id, case_id, role, name, firm, bar_number, address, phone, fax, email FROM case_contacts"
        " WHERE case_id = ? ORDER BY position, id");
    q.bind(1, caseId);
    std::vector<CaseContact> out;
    while (q.executeStep()) out.push_back(readContact(q));
    return out;
}

std::int64_t Database::addContact(const CaseContact& c)
{
    if (!getCase(c.caseId)) throw DatabaseError("That case no longer exists.");
    SQLite::Statement q(impl_->db,
        "INSERT INTO case_contacts (role, name, firm, bar_number, address, phone, fax, email, case_id, position)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, (SELECT COALESCE(MAX(position), -1) + 1 FROM case_contacts WHERE case_id = ?))");
    bindContact(q, c);
    q.bind(9, c.caseId);
    q.bind(10, c.caseId);
    q.exec();
    return impl_->db.getLastInsertRowid();
}

void Database::updateContact(const CaseContact& c)
{
    SQLite::Statement q(impl_->db,
        "UPDATE case_contacts SET role = ?, name = ?, firm = ?, bar_number = ?, address = ?, phone = ?, fax = ?,"
        " email = ? WHERE id = ?");
    bindContact(q, c);
    q.bind(9, c.id);
    if (q.exec() == 0) throw DatabaseError("That person no longer exists.");
}

void Database::deleteContact(std::int64_t contactId)
{
    SQLite::Statement q(impl_->db, "DELETE FROM case_contacts WHERE id = ?");
    q.bind(1, contactId);
    q.exec();
}

// ---- Attorneys on a case ----------------------------------------------------------

std::vector<Attorney> Database::listCaseAttorneys(std::int64_t caseId) const
{
    SQLite::Statement q(impl_->db,
        "SELECT a.id, a.firm_id, a.name, a.bar_number, a.email, a.sign_off FROM case_attorneys ca"
        " JOIN attorneys a ON a.id = ca.attorney_id JOIN firms f ON f.id = a.firm_id"
        " WHERE ca.case_id = ? ORDER BY f.is_ours DESC, f.name COLLATE NOCASE, a.name COLLATE NOCASE, a.id");
    q.bind(1, caseId);
    std::vector<Attorney> out;
    while (q.executeStep())
        out.push_back({q.getColumn(0).getInt64(), q.getColumn(1).getInt64(), q.getColumn(2).getString(),
                       q.getColumn(3).getString(), q.getColumn(4).getString(), q.getColumn(5).getString()});
    return out;
}

void Database::setCaseAttorneys(std::int64_t caseId, const std::vector<std::int64_t>& attorneyIds)
{
    if (!getCase(caseId)) throw DatabaseError("That case no longer exists.");
    SQLite::Transaction tx(impl_->db);
    SQLite::Statement del(impl_->db, "DELETE FROM case_attorneys WHERE case_id = ?");
    del.bind(1, caseId);
    del.exec();
    for (const auto id : attorneyIds) {
        SQLite::Statement check(impl_->db, "SELECT COUNT(*) FROM attorneys WHERE id = ?");
        check.bind(1, id);
        check.executeStep();
        if (check.getColumn(0).getInt() == 0)
            throw DatabaseError("One of the attorneys was deleted from the directory. Choose the attorneys again.");
        SQLite::Statement ins(impl_->db, "INSERT OR IGNORE INTO case_attorneys (case_id, attorney_id) VALUES (?, ?)");
        ins.bind(1, caseId);
        ins.bind(2, id);
        ins.exec();
    }
    tx.commit();
}

void Database::removeCaseAttorney(std::int64_t caseId, std::int64_t attorneyId)
{
    SQLite::Statement q(impl_->db, "DELETE FROM case_attorneys WHERE case_id = ? AND attorney_id = ?");
    q.bind(1, caseId);
    q.bind(2, attorneyId);
    q.exec();
}

std::size_t Database::caseCountForAttorney(std::int64_t attorneyId) const
{
    SQLite::Statement q(impl_->db, "SELECT COUNT(*) FROM case_attorneys WHERE attorney_id = ?");
    q.bind(1, attorneyId);
    q.executeStep();
    return static_cast<std::size_t>(q.getColumn(0).getInt64());
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
    SQLite::Statement owned(impl_->db, "SELECT account_id FROM account_owners WHERE person_id = ?");
    owned.bind(1, personId);
    std::vector<std::int64_t> ids;
    while (owned.executeStep()) ids.push_back(owned.getColumn(0).getInt64());

    std::string accounts;
    for (const Account& a : impl_->loadAccounts(ids)) {
        if (!accounts.empty()) accounts += "; ";
        accounts += accountLabel(a);
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

std::int64_t Database::createAccount(const AccountRecord& input)
{
    const AccountRecord a = Impl::withPartOwners(input);
    impl_->requireCase(a.caseId);
    impl_->validate(a);

    const bool combined = a.isCombined();
    SQLite::Transaction tx(impl_->db);
    SQLite::Statement q(impl_->db,
        "INSERT INTO accounts (case_id, institution, account_type, last_four, institution_display, is_combined,"
        " opened_on, closed_on) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    q.bind(1, a.caseId);
    q.bind(2, trimmed(a.institution));
    q.bind(3, combined ? std::string() : trimmed(a.accountType));
    q.bind(4, combined ? std::string() : trimmed(a.lastFour));
    q.bind(5, institutionDisplayOrDefault(a));
    q.bind(6, combined ? 1 : 0);
    q.bind(7, combined ? std::string() : dateToText(a.openedOn));
    q.bind(8, combined ? std::string() : dateToText(a.closedOn));
    q.exec();
    const std::int64_t id = impl_->db.getLastInsertRowid();
    impl_->writeOwners(id, combined ? Impl::unionOwnerIds(a.parts) : a.ownerIds);
    if (combined) impl_->writeParts(id, a.parts);
    else impl_->writePreviousNumbers(id, a.previousLastFour);
    tx.commit();
    return id;
}

void Database::updateAccount(const AccountRecord& input, const NumberCorrections& numberCorrections)
{
    const AccountRecord a = Impl::withPartOwners(input);
    const auto existing = getAccount(a.id);
    if (!existing) throw DatabaseError("That account no longer exists.");

    if (existing->isCombined() != a.isCombined())
        throw DatabaseError("A combined statement can't be changed into a single account, or the other way round.");

    AccountRecord checked = a;
    checked.caseId = existing->caseId;  // accounts never move between cases
    impl_->validate(checked);

    SQLite::Transaction tx(impl_->db);
    const std::optional<Account> before = impl_->loadAccount(a.id);
    if (a.isCombined()) {
        SQLite::Statement u(impl_->db,
            "UPDATE accounts SET institution = ?, institution_display = ? WHERE id = ?");
        u.bind(1, trimmed(a.institution));
        u.bind(2, institutionDisplayOrDefault(a));
        u.bind(3, a.id);
        u.exec();
        impl_->writeOwners(a.id, Impl::unionOwnerIds(a.parts));
        impl_->writeParts(a.id, a.parts);
        if (before) impl_->recordOldNames(*before, *impl_->loadAccount(a.id));
        tx.commit();
        return;
    }

    SQLite::Statement q(impl_->db,
        "UPDATE accounts SET institution = ?, account_type = ?, last_four = ?, institution_display = ?,"
        " opened_on = ?, closed_on = ? WHERE id = ?");
    q.bind(1, trimmed(a.institution));
    q.bind(2, trimmed(a.accountType));
    q.bind(3, trimmed(a.lastFour));
    q.bind(4, institutionDisplayOrDefault(a));
    q.bind(5, dateToText(a.openedOn));
    q.bind(6, dateToText(a.closedOn));
    q.bind(7, a.id);
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
    return impl_->getRecord(accountId);
}

std::vector<AccountRecord> Database::listAccountRecords(std::int64_t caseId) const
{
    SQLite::Statement q(impl_->db, std::string("SELECT ") + kAccountColumns + R"sql( FROM accounts
        WHERE case_id = ?
        ORDER BY institution COLLATE NOCASE, account_type COLLATE NOCASE, last_four, id)sql");
    q.bind(1, caseId);
    std::vector<std::pair<AccountRecord, bool>> rows;
    while (q.executeStep()) rows.push_back({Impl::readAccountRow(q), q.getColumn(6).getInt() != 0});

    std::vector<AccountRecord> out;
    for (auto& [a, combined] : rows) {
        impl_->completeRecord(a, combined);
        out.push_back(std::move(a));
    }
    // Combined statements (stored without an institution) go after their
    // institution's accounts.
    std::stable_sort(out.begin(), out.end(), [](const AccountRecord& x, const AccountRecord& y) {
        const std::string kx = foldKey(x.institution), ky = foldKey(y.institution);
        if (kx != ky) return kx < ky;
        return !x.isCombined() && y.isCombined();
    });
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

// ---- File name templates -----------------------------------------------------

namespace {

std::string trimmedName(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

}  // namespace

std::vector<DocumentTemplate> Database::listTemplates() const
{
    SQLite::Statement q(impl_->db,
        "SELECT id, name, pattern, use_date, space_after_date FROM document_templates ORDER BY name COLLATE NOCASE, id");
    std::vector<DocumentTemplate> out;
    while (q.executeStep())
        out.push_back({q.getColumn(0).getInt64(), q.getColumn(1).getString(), q.getColumn(2).getString(),
                       q.getColumn(3).getInt() != 0, q.getColumn(4).getInt() != 0});
    return out;
}

static void checkTemplate(SQLite::Database& db, const DocumentTemplate& t, std::int64_t ownId)
{
    if (auto err = validateTemplate(t)) throw DatabaseError(*err);
    SQLite::Statement q(db, "SELECT COUNT(*) FROM document_templates WHERE name = ? COLLATE NOCASE AND id != ?");
    q.bind(1, trimmedName(t.name));
    q.bind(2, ownId);
    q.executeStep();
    if (q.getColumn(0).getInt() > 0)
        throw DatabaseError("There is already a template named \"" + trimmedName(t.name) + "\".");
}

std::int64_t Database::createTemplate(const DocumentTemplate& t)
{
    checkTemplate(impl_->db, t, 0);
    SQLite::Statement q(impl_->db, "INSERT INTO document_templates (name, pattern, use_date, space_after_date) VALUES (?, ?, ?, ?)");
    q.bind(1, trimmedName(t.name));
    q.bind(2, t.pattern);
    q.bind(3, t.useDate ? 1 : 0);
    q.bind(4, t.spaceAfterDate ? 1 : 0);
    q.exec();
    return impl_->db.getLastInsertRowid();
}

void Database::updateTemplate(const DocumentTemplate& t)
{
    checkTemplate(impl_->db, t, t.id);
    SQLite::Statement q(impl_->db, "UPDATE document_templates SET name = ?, pattern = ?, use_date = ?, space_after_date = ? WHERE id = ?");
    q.bind(1, trimmedName(t.name));
    q.bind(2, t.pattern);
    q.bind(3, t.useDate ? 1 : 0);
    q.bind(4, t.spaceAfterDate ? 1 : 0);
    q.bind(5, t.id);
    if (q.exec() == 0) throw DatabaseError("That template no longer exists.");
}

void Database::deleteTemplate(std::int64_t id)
{
    SQLite::Statement q(impl_->db, "DELETE FROM document_templates WHERE id = ?");
    q.bind(1, id);
    q.exec();
}

// ---- Attorney directory -----------------------------------------------------

static Firm readFirm(SQLite::Statement& q)
{
    return {q.getColumn(0).getInt64(), q.getColumn(1).getString(), q.getColumn(2).getString(),
            q.getColumn(3).getString(), q.getColumn(4).getString(), q.getColumn(5).getInt() != 0};
}

static Attorney readAttorney(SQLite::Statement& q)
{
    return {q.getColumn(0).getInt64(), q.getColumn(1).getInt64(), q.getColumn(2).getString(),
            q.getColumn(3).getString(), q.getColumn(4).getString(), q.getColumn(5).getString()};
}

static void requireFirm(SQLite::Database& db, std::int64_t id)
{
    SQLite::Statement q(db, "SELECT COUNT(*) FROM firms WHERE id = ?");
    q.bind(1, id);
    q.executeStep();
    if (q.getColumn(0).getInt() == 0) throw DatabaseError("That firm no longer exists.");
}

static void checkFirm(SQLite::Database& db, const Firm& f, std::int64_t ownId)
{
    const std::string name = trimmedName(f.name);
    if (name.empty()) throw DatabaseError("Type the firm's name.");
    SQLite::Statement q(db, "SELECT COUNT(*) FROM firms WHERE name = ? COLLATE NOCASE AND id != ?");
    q.bind(1, name);
    q.bind(2, ownId);
    q.executeStep();
    if (q.getColumn(0).getInt() > 0) throw DatabaseError("There is already a firm named \"" + name + "\".");
}

static void checkAttorney(SQLite::Database& db, const Attorney& a)
{
    if (trimmedName(a.name).empty()) throw DatabaseError("Type the attorney's name.");
    requireFirm(db, a.firmId);
}

std::vector<Firm> Database::listFirms() const
{
    SQLite::Statement q(impl_->db,
        "SELECT id, name, phone, fax, address, is_ours FROM firms ORDER BY name COLLATE NOCASE, id");
    std::vector<Firm> out;
    while (q.executeStep()) out.push_back(readFirm(q));
    return out;
}

std::int64_t Database::createFirm(const Firm& f)
{
    checkFirm(impl_->db, f, 0);
    SQLite::Statement q(impl_->db, "INSERT INTO firms (name, phone, fax, address, is_ours) VALUES (?, ?, ?, ?, ?)");
    q.bind(1, trimmedName(f.name));
    q.bind(2, trimmedName(f.phone));
    q.bind(3, trimmedName(f.fax));
    q.bind(4, trimmedName(f.address));
    q.bind(5, f.ours ? 1 : 0);
    q.exec();
    return impl_->db.getLastInsertRowid();
}

void Database::updateFirm(const Firm& f)
{
    checkFirm(impl_->db, f, f.id);
    SQLite::Statement q(impl_->db, "UPDATE firms SET name = ?, phone = ?, fax = ?, address = ?, is_ours = ? WHERE id = ?");
    q.bind(1, trimmedName(f.name));
    q.bind(2, trimmedName(f.phone));
    q.bind(3, trimmedName(f.fax));
    q.bind(4, trimmedName(f.address));
    q.bind(5, f.ours ? 1 : 0);
    q.bind(6, f.id);
    if (q.exec() == 0) throw DatabaseError("That firm no longer exists.");
}

void Database::deleteFirm(std::int64_t firmId)
{
    SQLite::Statement c(impl_->db, "SELECT COUNT(*) FROM attorneys WHERE firm_id = ?");
    c.bind(1, firmId);
    c.executeStep();
    if (c.getColumn(0).getInt() > 0)
        throw DatabaseError("This firm still has attorneys. Move or delete them first.");
    SQLite::Statement q(impl_->db, "DELETE FROM firms WHERE id = ?");
    q.bind(1, firmId);
    q.exec();
}

std::vector<Attorney> Database::listAttorneys(std::int64_t firmId) const
{
    SQLite::Statement q(impl_->db,
        "SELECT id, firm_id, name, bar_number, email, sign_off FROM attorneys WHERE firm_id = ? ORDER BY name COLLATE NOCASE, id");
    q.bind(1, firmId);
    std::vector<Attorney> out;
    while (q.executeStep()) out.push_back(readAttorney(q));
    return out;
}

std::vector<Attorney> Database::listAllAttorneys() const
{
    SQLite::Statement q(impl_->db,
        "SELECT id, firm_id, name, bar_number, email, sign_off FROM attorneys ORDER BY name COLLATE NOCASE, id");
    std::vector<Attorney> out;
    while (q.executeStep()) out.push_back(readAttorney(q));
    return out;
}

std::int64_t Database::createAttorney(const Attorney& a)
{
    checkAttorney(impl_->db, a);
    SQLite::Statement q(impl_->db,
        "INSERT INTO attorneys (firm_id, name, bar_number, email, sign_off) VALUES (?, ?, ?, ?, ?)");
    q.bind(1, a.firmId);
    q.bind(2, trimmedName(a.name));
    q.bind(3, trimmedName(a.barNumber));
    q.bind(4, trimmedName(a.email));
    q.bind(5, trimmedName(a.signOff));
    q.exec();
    return impl_->db.getLastInsertRowid();
}

void Database::updateAttorney(const Attorney& a)
{
    checkAttorney(impl_->db, a);
    SQLite::Statement q(impl_->db,
        "UPDATE attorneys SET firm_id = ?, name = ?, bar_number = ?, email = ?, sign_off = ? WHERE id = ?");
    q.bind(1, a.firmId);
    q.bind(2, trimmedName(a.name));
    q.bind(3, trimmedName(a.barNumber));
    q.bind(4, trimmedName(a.email));
    q.bind(5, trimmedName(a.signOff));
    q.bind(6, a.id);
    if (q.exec() == 0) throw DatabaseError("That attorney no longer exists.");
}

void Database::moveAttorney(std::int64_t attorneyId, std::int64_t newFirmId)
{
    requireFirm(impl_->db, newFirmId);
    SQLite::Statement q(impl_->db, "UPDATE attorneys SET firm_id = ? WHERE id = ?");
    q.bind(1, newFirmId);
    q.bind(2, attorneyId);
    if (q.exec() == 0) throw DatabaseError("That attorney no longer exists.");
}

std::size_t Database::moveAllAttorneys(std::int64_t fromFirmId, std::int64_t toFirmId)
{
    requireFirm(impl_->db, fromFirmId);
    requireFirm(impl_->db, toFirmId);
    SQLite::Statement q(impl_->db, "UPDATE attorneys SET firm_id = ? WHERE firm_id = ?");
    q.bind(1, toFirmId);
    q.bind(2, fromFirmId);
    return static_cast<std::size_t>(q.exec());
}

void Database::deleteAttorney(std::int64_t attorneyId)
{
    SQLite::Statement q(impl_->db, "DELETE FROM attorneys WHERE id = ?");
    q.bind(1, attorneyId);
    q.exec();
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

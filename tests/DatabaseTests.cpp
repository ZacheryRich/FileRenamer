#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "TestHelpers.h"
#include "finrenamer/Database.h"
#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/FolderPlanner.h"

#include <SQLiteCpp/SQLiteCpp.h>

using namespace finrenamer;
namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;

namespace {

struct SmithCase {
    Database db = Database::openInMemory();
    std::int64_t caseId = db.createCase({0, "Smith Divorce", "Filed 2026"});
    std::int64_t john = db.addPerson(caseId, "John Smith");
    std::int64_t jane = db.addPerson(caseId, "Jane Smith");

    std::int64_t addAccount(std::string inst, std::string type, std::string four,
                            std::vector<std::int64_t> owners)
    {
        return db.createAccount({0, caseId, inst, "", type, four, owners});
    }
};

}  // namespace

TEST_CASE("A new database is created at the current schema version")
{
    testing::TempDir dir;
    const fs::path file = dir.path() / "finrenamer.db";
    {
        Database db(file);
        CHECK(db.schemaVersion() == 10);
        db.createCase({0, "Persisted", ""});
    }
    CHECK(fs::exists(file));

    Database reopened(file);  // reopening must not re-run migrations
    CHECK(reopened.schemaVersion() == 10);
    REQUIRE(reopened.listCases().size() == 1);
    CHECK(reopened.listCases()[0].clientName == "Persisted");
}

TEST_CASE("Database files in folders with accented names open correctly")
{
    testing::TempDir dir;
    const fs::path folder = dir.path() / pathFromUtf8("Núñez");
    fs::create_directories(folder);
    Database db(folder / "finrenamer.db");
    CHECK(fs::exists(folder / "finrenamer.db"));
}

TEST_CASE("Cases: create, list sorted, update, require a name")
{
    Database db = Database::openInMemory();
    const auto b = db.createCase({0, "  zeta estate  ", ""});
    db.createCase({0, "Alpha Trust", "notes"});

    auto cases = db.listCases();
    REQUIRE(cases.size() == 2);
    CHECK(cases[0].clientName == "Alpha Trust");
    CHECK(cases[1].clientName == "zeta estate");  // trimmed, case-insensitive sort

    db.updateCase({b, "Zeta Estate", "updated"});
    CHECK(db.getCase(b)->notes == "updated");

    CHECK_THROWS_AS(db.createCase({0, "   ", ""}), DatabaseError);
    CHECK_THROWS_AS(db.updateCase({9999, "Nobody", ""}), DatabaseError);
    CHECK_FALSE(db.getCase(9999));
}

TEST_CASE("People: names are unique per case, ignoring letter case")
{
    SmithCase s;
    CHECK_THROWS_WITH(s.db.addPerson(s.caseId, "john smith"),
                      ContainsSubstring("already listed"));

    const auto other = s.db.createCase({0, "Other Client", ""});
    CHECK_NOTHROW(s.db.addPerson(other, "John Smith"));  // fine on another case

    s.db.updatePerson(s.john, "John Q. Smith", "");
    CHECK_THROWS_AS(s.db.updatePerson(s.john, "JANE SMITH", ""), DatabaseError);
    CHECK_NOTHROW(s.db.updatePerson(s.john, "john q. smith", ""));  // own name, new casing

    const auto people = s.db.listPeople(s.caseId);  // in the order they were added
    REQUIRE(people.size() == 2);
    CHECK(people[0].fullName == "john q. smith");
    CHECK(people[1].fullName == "Jane Smith");
}

TEST_CASE("Accounts keep owners in display order")
{
    SmithCase s;
    const auto id = s.addAccount("Chase", "Checking", "1234", {s.john, s.jane});

    auto accounts = s.db.loadAccounts(s.caseId);
    REQUIRE(accounts.size() == 1);
    CHECK(accounts[0].owners == std::vector<std::string>{"John Smith", "Jane Smith"});
    CHECK(accountLabel(accounts[0]) == "Chase Checking x1234 (John Smith; Jane Smith)");

    s.db.updateAccount({id, s.caseId, "Chase", "", "Checking", "1234", {s.jane, s.john}});
    accounts = s.db.loadAccounts(s.caseId);
    CHECK(accounts[0].owners == std::vector<std::string>{"Jane Smith", "John Smith"});

    s.db.updateAccount({id, s.caseId, "Chase", "", "Checking", "1234", {}});
    CHECK(s.db.loadAccounts(s.caseId)[0].owners.empty());
}

TEST_CASE("Renaming a person updates every account they own")
{
    SmithCase s;
    s.addAccount("Chase", "Checking", "1234", {s.john, s.jane});
    s.addAccount("Fidelity", "Brokerage", "5678", {s.john});

    s.db.updatePerson(s.john, "Jonathan Smith", "");
    for (const auto& a : s.db.loadAccounts(s.caseId))
        CHECK(a.owners.front() == "Jonathan Smith");
}

TEST_CASE("Saving an account validates it and its owners")
{
    SmithCase s;
    CHECK_THROWS_AS(s.addAccount("", "Checking", "1234", {}), DatabaseError);
    CHECK_THROWS_AS(s.addAccount("Chase", "Checking", "", {}), DatabaseError);
    CHECK_THROWS_WITH(s.addAccount("Chase", "Checking", "1234", {s.john, s.john}),
                      ContainsSubstring("twice"));

    const auto other = s.db.createCase({0, "Other", ""});
    const auto stranger = s.db.addPerson(other, "Stranger");
    CHECK_THROWS_WITH(s.addAccount("Chase", "Checking", "1234", {stranger}),
                      ContainsSubstring("different case"));

    CHECK(s.db.listAccountRecords(s.caseId).empty());  // nothing half-saved
}

TEST_CASE("A person who owns an account can't be deleted")
{
    SmithCase s;
    const auto acct = s.addAccount("Chase", "Checking", "1234", {s.john});

    CHECK_THROWS_WITH(s.db.deletePerson(s.john), ContainsSubstring("Chase Checking x1234"));
    CHECK_NOTHROW(s.db.deletePerson(s.jane));  // owns nothing

    s.db.deleteAccount(acct);
    CHECK_NOTHROW(s.db.deletePerson(s.john));
    CHECK(s.db.listPeople(s.caseId).empty());
}

TEST_CASE("Deleting a case removes its people and accounts but keeps rename history")
{
    SmithCase s;
    s.addAccount("Chase", "Checking", "1234", {s.john, s.jane});

    BatchRecord batch;
    batch.batchId = "b1";
    batch.root = "C:/Clients/Smith";
    batch.moves = {{"C:/Clients/Smith/a.pdf", "C:/Clients/Smith/b.pdf"}};
    s.db.saveBatch(batch, s.caseId);

    s.db.deleteCase(s.caseId);
    CHECK(s.db.listCases().empty());
    CHECK(s.db.listPeople(s.caseId).empty());
    CHECK(s.db.listAccountRecords(s.caseId).empty());

    const auto history = s.db.listBatches();
    REQUIRE(history.size() == 1);
    CHECK_FALSE(history[0].caseId.has_value());  // case link cleared, history kept
}

TEST_CASE("A saved batch reloads exactly and can still be undone after reopening")
{
    testing::TempDir work;
    testing::TempDir data;
    const fs::path dbFile = data.path() / "finrenamer.db";

    const fs::path src = work.touch("estado.pdf", "PDF");
    std::string batchId;
    {
        Database db(dbFile);
        const auto caseId = db.createCase({0, "Núñez", ""});
        const auto jose = db.addPerson(caseId, "José Núñez");
        db.createAccount({0, caseId, "Banco Popular", "", "Ahorros", "9876", {jose}});
        const auto accounts = db.loadAccounts(caseId);

        PlanOptions opts;
        opts.sort.byAccount = true;
        const auto plan = buildPlan(work.path(), {PlanInput{src, accounts[0], Quarter{2026, 2}}}, opts);
        const auto result = execute(plan);
        REQUIRE(result.failures.empty());

        db.saveBatch(result.record, caseId);
        batchId = result.record.batchId;
    }

    // Program closed and reopened.
    Database db(dbFile);
    const auto history = db.listBatches();
    REQUIRE(history.size() == 1);
    CHECK(history[0].moveCount == 1);
    CHECK_FALSE(history[0].undone);
    CHECK(history[0].createdAt.size() == 20);  // 2026-10-03T18:09:00Z

    const auto record = db.loadBatch(batchId);
    REQUIRE(record);
    REQUIRE(record->moves.size() == 1);
    CHECK(record->moves[0].from == src);
    REQUIRE(record->createdFolders.size() == 1);

    const auto undone = undo(*record);
    CHECK(undone.restored == 1);
    CHECK(undone.foldersRemoved == 1);
    CHECK(testing::readFile(src) == "PDF");

    db.markBatchUndone(batchId);
    CHECK(db.listBatches()[0].undone);
    CHECK_FALSE(db.loadBatch("missing"));
}

TEST_CASE("History lists the newest batch first")
{
    Database db = Database::openInMemory();
    for (const char* id : {"first", "second", "third"}) {
        BatchRecord b;
        b.batchId = id;
        b.root = "C:/x";
        db.saveBatch(b, std::nullopt);
    }
    const auto history = db.listBatches(2);
    REQUIRE(history.size() == 2);
    CHECK(history[0].batchId == "third");
    CHECK(history[1].batchId == "second");
}

TEST_CASE("Display names are used in filenames; blank means the full name")
{
    SmithCase s;
    const auto p = s.db.listPeople(s.caseId);
    CHECK(p[0].displayName == "John Smith");  // added without a display name

    s.db.updatePerson(s.john, "John Smith", " JS ");
    s.addAccount("Chase", "Checking", "1234", {s.john, s.jane});
    const auto accounts = s.db.loadAccounts(s.caseId);
    CHECK(accountLabel(accounts[0]) == "Chase Checking x1234 (JS; Jane Smith)");
    CHECK(s.db.listPeople(s.caseId)[0].fullName == "John Smith");
}

TEST_CASE("No two people on a case can show the same display name")
{
    SmithCase s;
    s.db.updatePerson(s.john, "John Smith", "J");
    CHECK_THROWS_WITH(s.db.addPerson(s.caseId, "Joint", "j"),
                      ContainsSubstring("John Smith already shows as"));
    CHECK_THROWS_AS(s.db.updatePerson(s.jane, "Jane Smith", "J"), DatabaseError);
    // A display name may match someone's full name only if it is that same person.
    CHECK_THROWS_AS(s.db.addPerson(s.caseId, "Jonathan", "Jane Smith"), DatabaseError);

    const auto other = s.db.createCase({0, "Other", ""});
    CHECK_NOTHROW(s.db.addPerson(other, "Joint", "J"));  // other cases are separate
}

TEST_CASE("New cases can start with Husband, Wife and Joint")
{
    Database db = Database::openInMemory();
    const auto id = db.createCase({0, "Smith Divorce", ""}, defaultCasePeople());

    const auto people = db.listPeople(id);
    REQUIRE(people.size() == 3);
    CHECK(people[0].fullName == "Husband");
    CHECK(people[0].displayName == "H");
    CHECK(people[1].fullName == "Wife");
    CHECK(people[1].displayName == "W");
    CHECK(people[2].fullName == "Joint");
    CHECK(people[2].displayName == "J");

    db.createAccount({0, id, "Chase", "", "Checking", "1234", {people[2].id}});
    CHECK(accountLabel(db.loadAccounts(id)[0]) == "Chase Checking x1234 (J)");

    // Defaults are ordinary people: they can be renamed or removed.
    db.updatePerson(people[0].id, "John Smith", "H");
    db.deletePerson(people[1].id);
    CHECK(db.listPeople(id).size() == 2);

    // Without the list, a case starts empty.
    CHECK(db.listPeople(db.createCase({0, "Empty", ""})).empty());
}

TEST_CASE("A version 1 database is upgraded and keeps its data")
{
    testing::TempDir dir;
    const fs::path file = dir.path() / "old.db";
    {
        // The schema exactly as the first release created it.
        SQLite::Database old(utf8FromPath(file), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        old.exec(R"sql(
            CREATE TABLE cases (id INTEGER PRIMARY KEY, client_name TEXT NOT NULL, notes TEXT NOT NULL DEFAULT '');
            CREATE TABLE people (id INTEGER PRIMARY KEY,
                case_id INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
                full_name TEXT NOT NULL COLLATE NOCASE, UNIQUE (case_id, full_name));
            CREATE TABLE accounts (id INTEGER PRIMARY KEY,
                case_id INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
                institution TEXT NOT NULL, account_type TEXT NOT NULL, last_four TEXT NOT NULL);
            CREATE TABLE account_owners (
                account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
                person_id INTEGER NOT NULL REFERENCES people(id) ON DELETE RESTRICT,
                position INTEGER NOT NULL, PRIMARY KEY (account_id, person_id));
            CREATE TABLE rename_batches (batch_id TEXT PRIMARY KEY,
                case_id INTEGER REFERENCES cases(id) ON DELETE SET NULL, root TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
                undone INTEGER NOT NULL DEFAULT 0);
            CREATE TABLE rename_log (
                batch_id TEXT NOT NULL REFERENCES rename_batches(batch_id) ON DELETE CASCADE,
                seq INTEGER NOT NULL, from_path TEXT NOT NULL, to_path TEXT NOT NULL,
                PRIMARY KEY (batch_id, seq));
            CREATE TABLE created_folders (
                batch_id TEXT NOT NULL REFERENCES rename_batches(batch_id) ON DELETE CASCADE,
                seq INTEGER NOT NULL, path TEXT NOT NULL, PRIMARY KEY (batch_id, seq));
            INSERT INTO cases (id, client_name) VALUES (1, 'Old Case');
            INSERT INTO people (id, case_id, full_name) VALUES (1, 1, 'José Núñez');
            INSERT INTO accounts (id, case_id, institution, account_type, last_four)
                VALUES (1, 1, 'Chase', 'Checking', '1234');
            INSERT INTO account_owners VALUES (1, 1, 0);
            DROP TABLE IF EXISTS document_templates;
            PRAGMA user_version = 1;
        )sql");
    }

    Database db(file);
    CHECK(db.schemaVersion() == 10);
    const auto people = db.listPeople(1);
    REQUIRE(people.size() == 1);
    CHECK(people[0].displayName == "José Núñez");
    CHECK(accountLabel(db.loadAccounts(1)[0]) == "Chase Checking x1234 (José Núñez)");

    db.updatePerson(1, "José Núñez", "JN");
    CHECK(accountLabel(db.loadAccounts(1)[0]) == "Chase Checking x1234 (JN)");

    // Version 3 gave existing accounts their institution as the display name.
    CHECK(db.getAccount(1)->institutionDisplay == "Chase");
}

TEST_CASE("Institution display names are used in filenames and folder names")
{
    SmithCase s;
    const auto id = s.db.createAccount({0, s.caseId, "Bank of America", "BofA", "Checking", "1234", {s.john}});

    const auto record = s.db.getAccount(id);
    CHECK(record->institution == "Bank of America");
    CHECK(record->institutionDisplay == "BofA");

    const auto account = s.db.loadAccounts(s.caseId)[0];
    CHECK(accountLabel(account) == "BofA Checking x1234 (John Smith)");
    SortOptions byAccount;
    byAccount.byAccount = true;
    CHECK(subfolderFor(account, Quarter{2026, 1}, byAccount) == fs::path("BofA Checking x1234 (John Smith)"));

    // Blank display name = the full institution.
    s.db.updateAccount({id, s.caseId, "Bank of America", "  ", "Checking", "1234", {s.john}});
    CHECK(s.db.getAccount(id)->institutionDisplay == "Bank of America");
    CHECK(accountLabel(s.db.loadAccounts(s.caseId)[0]) == "Bank of America Checking x1234 (John Smith)");
}

TEST_CASE("Previous numbers are stored, validated and loaded")
{
    SmithCase s;
    const auto id = s.db.createAccount({0, s.caseId, "Chase", "", "Credit Card", "9012", {s.john}, {"5678", "1234"}});
    CHECK(s.db.getAccount(id)->previousLastFour == std::vector<std::string>{"5678", "1234"});
    CHECK(s.db.loadAccounts(s.caseId)[0].previousLastFour == std::vector<std::string>{"5678", "1234"});
    CHECK(accountFolderLabel(s.db.loadAccounts(s.caseId)[0]) ==
          "Chase Credit Card x9012 (was x5678, x1234) (John Smith)");

    CHECK_THROWS_WITH(s.db.updateAccount({id, s.caseId, "Chase", "", "Credit Card", "9012", {}, {"9012"}}),
                      ContainsSubstring("more than once"));
    CHECK_THROWS_AS(s.db.updateAccount({id, s.caseId, "Chase", "", "Credit Card", "9012", {}, {"1234", "1234"}}),
                    DatabaseError);
    CHECK_THROWS_AS(s.db.updateAccount({id, s.caseId, "Chase", "", "Credit Card", "9012", {}, {" "}}),
                    DatabaseError);

    s.db.updateAccount({id, s.caseId, "Chase", "", "Credit Card", "9012", {s.john}, {}});
    CHECK(s.db.getAccount(id)->previousLastFour.empty());
}

TEST_CASE("Edits that change names are remembered as old names")
{
    SmithCase s;
    const auto id = s.db.createAccount({0, s.caseId, "Chsae", "", "Checking", "1234", {s.john}});
    CHECK(s.db.oldAccountNames(s.caseId).empty());

    // Fix the typo.
    s.db.updateAccount({id, s.caseId, "Chase", "", "Checking", "1234", {s.john}});
    auto old = s.db.oldAccountNames(s.caseId);
    REQUIRE(old.size() == 2);  // the file label and the folder name it had
    CHECK(old[0].name == "Chsae Checking x1234 (John Smith)");
    CHECK(old[0].lastFour == "1234");

    // Card replaced: 1234 becomes a previous number. File names for 1234 stay
    // valid, so only the folder name is new history.
    s.db.updateAccount({id, s.caseId, "Chase", "", "Checking", "5678", {s.john}, {"1234"}});
    old = s.db.oldAccountNames(s.caseId);
    REQUIRE(old.size() == 3);
    CHECK(old[2].isFolder);
    CHECK(old[2].name == "Chase Checking x1234 (John Smith)");

    // A person's new display name changes the names of their accounts.
    s.db.updatePerson(s.john, "John Smith", "H");
    old = s.db.oldAccountNames(s.caseId);
    bool found = false;
    for (const auto& o : old)
        if (!o.isFolder && o.name == "Chase Checking x5678 (John Smith)") found = true;
    CHECK(found);

    // Saving without changes records nothing new.
    const auto count = s.db.oldAccountNames(s.caseId).size();
    s.db.updateAccount(*s.db.getAccount(id));
    CHECK(s.db.oldAccountNames(s.caseId).size() == count);
}

// ---- Combined statements ---------------------------------------------------

TEST_CASE("Combined statements are saved and named from the typed list")
{
    SmithCase s;
    const auto chk = s.db.createAccount({0, s.caseId, "JPMorgan Chase", "Chase", "Chk", "1111", {s.john}});
    s.addAccount("Ally", "Savings", "9999", {});

    AccountRecord c;
    c.caseId = s.caseId;
    c.institution = "JPMorgan Chase";
    c.institutionDisplay = "Chase";
    c.ownerIds = {s.jane, s.john};
    c.parts = {{"Chk", "1111"}, {"Sav", "2222"}, {"Chk", "3333"}};  // none need to exist as accounts
    const auto id = s.db.createAccount(c);

    const auto rec = s.db.getAccount(id);
    REQUIRE(rec);
    CHECK(rec->isCombined());
    REQUIRE(rec->parts.size() == 3);
    CHECK(rec->parts[1].accountType == "Sav");
    CHECK(rec->parts[1].lastFour == "2222");
    CHECK(rec->institution == "JPMorgan Chase");
    CHECK(rec->institutionDisplay == "Chase");
    CHECK(rec->ownerIds == std::vector<std::int64_t>{s.jane, s.john});

    const auto accounts = s.db.loadAccounts(s.caseId);
    REQUIRE(accounts.size() == 3);
    CHECK(accountLabel(accounts[0]) == "Ally Savings x9999");
    CHECK(accounts[1].id == chk);
    const Account& combined = accounts.back();  // after the institution's own accounts
    REQUIRE(combined.isCombined());
    CHECK(accountLabel(combined) == "Chase Chk x1111, Sav x2222, Chk x3333 (Jane Smith; John Smith)");
    CHECK(accountFolderLabel(combined) == accountLabel(combined));

    // Reorder, drop one, change the institution's display name.
    c.id = id;
    c.parts = {{"Sav", "2222"}, {"Chk", "1111"}};
    c.ownerIds = {s.john};
    c.institutionDisplay = "JPM";
    s.db.updateAccount(c);
    CHECK(accountLabel(s.db.loadAccounts(s.caseId).back()) == "JPM Sav x2222, Chk x1111 (John Smith)");
}

TEST_CASE("Combined statement rules")
{
    SmithCase s;
    const auto chk = s.addAccount("Chase", "Chk", "1111", {});

    AccountRecord c;
    c.caseId = s.caseId;
    c.institution = "Chase";
    c.parts = {{"Chk", "1111"}};
    CHECK_THROWS_WITH(s.db.createAccount(c), ContainsSubstring("at least two"));
    c.parts = {{"Chk", "1111"}, {"chk", "1111"}};
    CHECK_THROWS_WITH(s.db.createAccount(c), ContainsSubstring("listed twice"));
    c.parts = {{"Chk", "1111"}, {"Sav", ""}};
    CHECK_THROWS_WITH(s.db.createAccount(c), ContainsSubstring("no number"));
    c.parts = {{"Chk", "1111"}, {"Sav", "2222"}};
    c.institution = "  ";
    CHECK_THROWS_WITH(s.db.createAccount(c), ContainsSubstring("needs an institution"));
    c.institution = "Chase";
    c.ownerIds = {999999};
    CHECK_THROWS_WITH(s.db.createAccount(c), ContainsSubstring("owner"));
    c.ownerIds = {};

    // Same numbers as an existing account is fine; they're just typed text. A blank type is fine too.
    c.parts = {{"Chk", "1111"}, {"", "2222"}};
    const auto id = s.db.createAccount(c);
    CHECK(accountLabel(s.db.loadAccounts(s.caseId).back()) == "Chase Chk x1111, x2222");

    // A single account can't become a combined statement or the reverse.
    AccountRecord single = *s.db.getAccount(chk);
    single.parts = {{"Chk", "1111"}, {"Sav", "2222"}};
    CHECK_THROWS_WITH(s.db.updateAccount(single), ContainsSubstring("can't be changed"));

    // Accounts and statements are deleted independently.
    s.db.deleteAccount(chk);
    CHECK(s.db.loadAccounts(s.caseId).size() == 1);
    s.db.deleteAccount(id);
    CHECK(s.db.loadAccounts(s.caseId).empty());

    // Deleting the whole case is fine.
    c.parts = {{"Chk", "1111"}, {"Sav", "5555"}};
    s.db.createAccount(c);
    s.db.deleteCase(s.caseId);
    CHECK(s.db.listAccountRecords(s.caseId).empty());
}

TEST_CASE("Editing a combined statement records its old names")
{
    SmithCase s;
    AccountRecord c;
    c.caseId = s.caseId;
    c.institution = "Chase";
    c.ownerIds = {s.john};
    c.parts = {{"Chekcing", "1111"}, {"Savings", "2222"}};
    const auto id = s.db.createAccount(c);

    auto namesOf = [&](std::int64_t accountId) {
        std::vector<std::pair<bool, std::string>> out;
        for (const auto& o : s.db.oldAccountNames(s.caseId))
            if (o.accountId == accountId) out.push_back({o.isFolder, o.name});
        return out;
    };

    // A typo in a type: files and the folder were named the old way.
    AccountRecord r = *s.db.getAccount(id);
    r.parts[0].accountType = "Checking";
    s.db.updateAccount(r);
    const std::vector<std::pair<bool, std::string>> typo{
        {false, "Chase Chekcing x1111, Savings x2222 (John Smith)"},
        {true, "Chase Chekcing x1111, Savings x2222 (John Smith)"}};
    CHECK(namesOf(id) == typo);

    // Renaming an owner of the statement.
    s.db.updatePerson(s.john, "John Smith", "H");
    const auto names = namesOf(id);
    REQUIRE(names.size() == 4);
    CHECK(names[2].second == "Chase Checking x1111, Savings x2222 (John Smith)");

    // Editing an unrelated single account leaves the statement alone.
    const auto chk = s.addAccount("Chase", "Checking", "1111", {});
    AccountRecord single = *s.db.getAccount(chk);
    single.accountType = "Chekcing";
    s.db.updateAccount(single);
    CHECK(namesOf(id).size() == 4);
}

TEST_CASE("A version 4 database is upgraded to combined statements")
{
    testing::TempDir dir;
    const fs::path file = dir.path() / "v4.db";
    {
        Database created(file);  // today's schema...
    }
    {
        // ...turned back into version 4.
        SQLite::Database old(utf8FromPath(file), SQLite::OPEN_READWRITE);
        old.exec(R"sql(
            DROP TABLE combined_parts;
            ALTER TABLE accounts DROP COLUMN is_combined;
            ALTER TABLE accounts DROP COLUMN opened_on;
            ALTER TABLE accounts DROP COLUMN closed_on;
            INSERT INTO cases (id, client_name) VALUES (1, 'Old Case');
            INSERT INTO accounts (id, case_id, institution, account_type, last_four, institution_display)
                VALUES (1, 1, 'Chase', 'Checking', '1234', 'Chase');
            DROP TABLE IF EXISTS document_templates;
            PRAGMA user_version = 4;
        )sql");
    }
    Database db(file);
    CHECK(db.schemaVersion() == 10);
    REQUIRE(db.loadAccounts(1).size() == 1);
    CHECK_FALSE(db.loadAccounts(1)[0].isCombined());
    AccountRecord c;
    c.caseId = 1;
    c.institution = "Chase";
    c.parts = {{"Checking", "1234"}, {"Savings", "5678"}};
    db.createAccount(c);
    CHECK(accountLabel(db.loadAccounts(1).back()) == "Chase Checking x1234, Savings x5678");
}

TEST_CASE("A version 6 database keeps its combined statements, now with typed parts")
{
    testing::TempDir dir;
    const fs::path file = dir.path() / "v6.db";
    {
        Database created(file);
    }
    {
        // ...turned back into version 6: statements linked to accounts, institution left blank.
        SQLite::Database old(utf8FromPath(file), SQLite::OPEN_READWRITE);
        old.exec(R"sql(
            DROP TABLE combined_parts;
            CREATE TABLE combined_members (
                combined_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
                account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
                position    INTEGER NOT NULL,
                PRIMARY KEY (combined_id, account_id)
            );
            INSERT INTO cases (id, client_name) VALUES (1, 'Old Case');
            INSERT INTO accounts (id, case_id, institution, account_type, last_four, institution_display)
                VALUES (1, 1, 'JPMorgan Chase', 'Checking', '1234', 'Chase'),
                       (2, 1, 'JPMorgan Chase', 'Savings', '5678', 'Chase');
            INSERT INTO accounts (id, case_id, institution, account_type, last_four, institution_display, is_combined)
                VALUES (3, 1, '', '', '', '', 1);
            INSERT INTO combined_members VALUES (3, 2, 0), (3, 1, 1);
            DROP TABLE IF EXISTS document_templates;
            PRAGMA user_version = 6;
        )sql");
    }
    Database db(file);
    CHECK(db.schemaVersion() == 10);
    const auto accounts = db.loadAccounts(1);
    REQUIRE(accounts.size() == 3);
    const Account& combined = accounts.back();
    REQUIRE(combined.isCombined());
    CHECK(accountLabel(combined) == "Chase Savings x5678, Checking x1234");
    CHECK(combined.institution == "JPMorgan Chase");

    // The linked accounts can now be deleted on their own.
    db.deleteAccount(1);
    CHECK(accountLabel(db.loadAccounts(1).back()) == "Chase Savings x5678, Checking x1234");
}

// ---- Opening and closing dates ----------------------------------------------

TEST_CASE("Accounts keep optional opening and closing dates")
{
    SmithCase s;
    AccountRecord a{0, s.caseId, "Chase", "", "Checking", "1234", {s.john}};
    CHECK_FALSE(s.db.getAccount(s.db.createAccount(a))->openedOn);  // both optional

    a.openedOn = makeDate(2022, 3, 15);
    a.closedOn = makeDate(2024, 11, 2);
    a.lastFour = "5678";
    const auto id = s.db.createAccount(a);
    auto r = *s.db.getAccount(id);
    CHECK(r.openedOn == makeDate(2022, 3, 15));
    CHECK(r.closedOn == makeDate(2024, 11, 2));
    for (const Account& loaded : s.db.loadAccounts(s.caseId))
        if (loaded.id == id) {
            CHECK(loaded.openedOn == makeDate(2022, 3, 15));
            CHECK(loaded.closedOn == makeDate(2024, 11, 2));
        }

    // Clear one, change the other. Dates don't change any file or folder name.
    r.closedOn.reset();
    r.openedOn = makeDate(2022, 1, 1);
    s.db.updateAccount(r);
    r = *s.db.getAccount(id);
    CHECK(r.openedOn == makeDate(2022, 1, 1));
    CHECK_FALSE(r.closedOn);
    CHECK(s.db.oldAccountNames(s.caseId).empty());

    // Closed before opened is refused (same day is fine).
    r.closedOn = makeDate(2021, 12, 31);
    CHECK_THROWS_WITH(s.db.updateAccount(r), ContainsSubstring("closed before it was opened"));
    r.closedOn = makeDate(2022, 1, 1);
    CHECK_NOTHROW(s.db.updateAccount(r));
}

TEST_CASE("A version 5 database is upgraded to opening and closing dates")
{
    testing::TempDir dir;
    const fs::path file = dir.path() / "v5.db";
    {
        Database created(file);
    }
    {
        SQLite::Database old(utf8FromPath(file), SQLite::OPEN_READWRITE);
        old.exec(R"sql(
            DROP TABLE combined_parts;
            CREATE TABLE combined_members (
                combined_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
                account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
                position    INTEGER NOT NULL,
                PRIMARY KEY (combined_id, account_id)
            );
            ALTER TABLE accounts DROP COLUMN opened_on;
            ALTER TABLE accounts DROP COLUMN closed_on;
            INSERT INTO cases (id, client_name) VALUES (1, 'Old Case');
            INSERT INTO accounts (id, case_id, institution, account_type, last_four, institution_display, is_combined)
                VALUES (1, 1, 'Chase', 'Checking', '1234', 'Chase', 0);
            DROP TABLE IF EXISTS document_templates;
            PRAGMA user_version = 5;
        )sql");
    }
    Database db(file);
    CHECK(db.schemaVersion() == 10);
    auto r = *db.getAccount(1);
    CHECK_FALSE(r.openedOn);
    CHECK_FALSE(r.closedOn);
    r.openedOn = makeDate(2020, 6, 1);
    db.updateAccount(r);
    CHECK(db.getAccount(1)->openedOn == makeDate(2020, 6, 1));
}

TEST_CASE("An account can be saved without an account type")
{
    Database db = Database::openInMemory();
    const auto caseId = db.createCase({0, "Smith", ""}, defaultCasePeople());
    const auto id = db.createAccount({0, caseId, "Chase", "", "", "1234", {}});
    const auto saved = db.getAccount(id);
    REQUIRE(saved);
    CHECK(saved->accountType.empty());

    // Editing keeps it blank, and a type can be added later.
    auto record = *saved;
    record.accountType = "Checking";
    db.updateAccount(record);
    CHECK(db.getAccount(id)->accountType == "Checking");
    record.accountType = "";
    db.updateAccount(record);
    CHECK(db.getAccount(id)->accountType.empty());
    CHECK(accountLabel(db.loadAccounts(caseId)[0]) == "Chase x1234");
}

TEST_CASE("Templates are saved, listed by name, validated and deleted")
{
    Database db = Database::openInMemory();
    CHECK(db.listTemplates().empty());

    const auto email = db.createTemplate({0, "Email", "Email from {Sender} to {Recipient}[ ({Description})]"});
    const auto text = db.createTemplate({0, "  Text Conversation ", "Texts between {A} and {B}"});
    auto all = db.listTemplates();
    REQUIRE(all.size() == 2);
    CHECK(all[0].name == "Email");
    CHECK(all[1].name == "Text Conversation");  // trimmed

    all[0].pattern = "Mail from {Sender}";
    db.updateTemplate(all[0]);
    CHECK(db.listTemplates()[0].pattern == "Mail from {Sender}");

    CHECK_THROWS_AS(db.createTemplate({0, "EMAIL", "x {A}"}), DatabaseError);   // name taken, any case
    CHECK_THROWS_AS(db.createTemplate({0, "Bad", "x {A"}), DatabaseError);      // bad pattern
    CHECK_THROWS_AS(db.createTemplate({0, " ", "x {A}"}), DatabaseError);       // no name
    CHECK_THROWS_AS(db.createTemplate({0, "Other", "x {A}"}), DatabaseError);   // built-in names
    auto renamed = db.listTemplates()[1];
    renamed.name = "email";
    CHECK_THROWS_AS(db.updateTemplate(renamed), DatabaseError);
    DocumentTemplate gone{9999, "Nope", "x"};
    CHECK_THROWS_AS(db.updateTemplate(gone), DatabaseError);

    db.deleteTemplate(email);
    REQUIRE(db.listTemplates().size() == 1);
    CHECK(db.listTemplates()[0].id == text);
}

TEST_CASE("A version 7 database is upgraded and gets an empty template list")
{
    testing::TempDir dir;
    const fs::path file = dir.path() / "v7.db";
    {
        Database created(file);
    }
    {
        SQLite::Database old(utf8FromPath(file), SQLite::OPEN_READWRITE);
        old.exec(R"sql(
            DROP TABLE document_templates;
            INSERT INTO cases (id, client_name) VALUES (1, 'Old Case');
            PRAGMA user_version = 7;
        )sql");
    }
    Database db(file);
    CHECK(db.schemaVersion() == 10);
    CHECK(db.getCase(1).has_value());
    CHECK(db.listTemplates().empty());
    CHECK_NOTHROW(db.createTemplate({0, "Letter", "Ltr from {Sender}"}));
}

TEST_CASE("Templates can leave the date out, and a version 8 database gets that setting")
{
    Database db = Database::openInMemory();
    const auto id = db.createTemplate({0, "Undated", "Photo of {Thing}", false});
    CHECK_FALSE(db.listTemplates()[0].useDate);
    auto t = db.listTemplates()[0];
    t.useDate = true;
    db.updateTemplate(t);
    CHECK(db.listTemplates()[0].useDate);
    (void)id;

    testing::TempDir dir;
    const fs::path file = dir.path() / "v8.db";
    {
        Database created(file);
    }
    {
        SQLite::Database old(utf8FromPath(file), SQLite::OPEN_READWRITE);
        old.exec(R"sql(
            DROP TABLE document_templates;
            CREATE TABLE document_templates (id INTEGER PRIMARY KEY, name TEXT NOT NULL, pattern TEXT NOT NULL,
                                             allow_period INTEGER NOT NULL DEFAULT 0);
            INSERT INTO document_templates (name, pattern, allow_period) VALUES ('Email', 'Email from {A}', 1);
            PRAGMA user_version = 8;
        )sql");
    }
    Database upgraded(file);
    CHECK(upgraded.schemaVersion() == 10);
    REQUIRE(upgraded.listTemplates().size() == 1);
    CHECK(upgraded.listTemplates()[0].useDate);  // existing templates keep their date
}

TEST_CASE("Upgrading an older database first leaves a backup copy beside it")
{
    testing::TempDir data;
    const fs::path dbFile = data.path() / "finrenamer.db";
    {
        Database db(dbFile);
        db.createCase({0, "Smith", ""});
    }
    CHECK_FALSE(fs::exists(data.path() / "finrenamer.db.backup-v10"));  // a new database needs none

    // Pretend the file is an older version, then open it again.
    {
        SQLite::Database raw(utf8FromPath(dbFile), SQLite::OPEN_READWRITE);
        raw.exec("DROP TABLE document_templates");
        raw.exec("PRAGMA user_version = 7");
    }
    {
        Database db(dbFile);
        CHECK(db.schemaVersion() == 10);
        CHECK(db.listCases().size() == 1);
    }
    const fs::path backup = data.path() / "finrenamer.db.backup-v7";
    REQUIRE(fs::exists(backup));
    {
        SQLite::Database old(utf8FromPath(backup), SQLite::OPEN_READONLY);
        CHECK(old.execAndGet("PRAGMA user_version").getInt() == 7);  // still the old version, old data
        CHECK(old.execAndGet("SELECT COUNT(*) FROM cases").getInt() == 1);
    }

    // Opening a current database makes no further backup.
    { Database db(dbFile); }
    CHECK_FALSE(fs::exists(data.path() / "finrenamer.db.backup-v10"));
    CHECK_FALSE(fs::exists(data.path() / "finrenamer.db.backup-v7-2"));
}

TEST_CASE("backupTo writes a complete copy that opens as a database, replacing an old file")
{
    testing::TempDir data;
    Database db(data.path() / "finrenamer.db");
    db.createCase({0, "Smith", ""});
    const fs::path copy = data.path() / "it's a copy.db";  // a quote in the name must be safe
    db.backupTo(copy);
    db.createCase({0, "Jones", ""});
    db.backupTo(copy);
    Database restored(copy);
    CHECK(restored.listCases().size() == 2);
    CHECK_FALSE(fs::exists(data.path() / "it's a copy.db.writing"));
}

TEST_CASE("Templates can attach to the date, and a version 9 database gets that setting")
{
    Database db = Database::openInMemory();
    db.createTemplate({0, "Tight", "_Photo of {Thing}", true, false});
    db.createTemplate({0, "Normal", "Photo of {Thing}"});
    for (const auto& t : db.listTemplates()) CHECK(t.spaceAfterDate == (t.name == "Normal"));
    auto t = db.listTemplates()[1];  // Tight
    t.spaceAfterDate = true;
    db.updateTemplate(t);
    CHECK(db.listTemplates()[1].spaceAfterDate);

    testing::TempDir dir;
    const fs::path file = dir.path() / "v9.db";
    { Database created(file); }
    {
        SQLite::Database old(utf8FromPath(file), SQLite::OPEN_READWRITE);
        old.exec(R"sql(
            DROP TABLE document_templates;
            CREATE TABLE document_templates (id INTEGER PRIMARY KEY, name TEXT NOT NULL, pattern TEXT NOT NULL,
                                             allow_period INTEGER NOT NULL DEFAULT 0, use_date INTEGER NOT NULL DEFAULT 1);
            INSERT INTO document_templates (name, pattern, allow_period, use_date) VALUES ('Email', 'Email from {A}', 1, 1);
            PRAGMA user_version = 9;
        )sql");
    }
    Database upgraded(file);
    CHECK(upgraded.schemaVersion() == 10);
    REQUIRE(upgraded.listTemplates().size() == 1);
    CHECK(upgraded.listTemplates()[0].spaceAfterDate);  // existing templates keep their space
    CHECK(fs::exists(dir.path() / "v9.db.backup-v9"));
}

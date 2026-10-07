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
        CHECK(db.schemaVersion() == 6);
        db.createCase({0, "Persisted", ""});
    }
    CHECK(fs::exists(file));

    Database reopened(file);  // reopening must not re-run migrations
    CHECK(reopened.schemaVersion() == 6);
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
    CHECK(accountLabel(accounts[0]) == "Chase Checking 1234 (John Smith; Jane Smith)");

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

    CHECK_THROWS_WITH(s.db.deletePerson(s.john), ContainsSubstring("Chase Checking 1234"));
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

TEST_CASE("Account type suggestions merge spellings that differ only in case")
{
    SmithCase s;
    s.addAccount("Chase", "Checking", "1", {});
    s.addAccount("Chase", "checking", "2", {});
    s.addAccount("Fidelity", "Roth IRA", "3", {});
    s.addAccount("Amex", "Credit Card", "4", {});

    const auto types = s.db.accountTypeSuggestions();
    REQUIRE(types.size() == 3);
    CHECK(types[1] == "Credit Card");
    CHECK(types[2] == "Roth IRA");
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
    CHECK(accountLabel(accounts[0]) == "Chase Checking 1234 (JS; Jane Smith)");
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
    CHECK(accountLabel(db.loadAccounts(id)[0]) == "Chase Checking 1234 (J)");

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
            PRAGMA user_version = 1;
        )sql");
    }

    Database db(file);
    CHECK(db.schemaVersion() == 6);
    const auto people = db.listPeople(1);
    REQUIRE(people.size() == 1);
    CHECK(people[0].displayName == "José Núñez");
    CHECK(accountLabel(db.loadAccounts(1)[0]) == "Chase Checking 1234 (José Núñez)");

    db.updatePerson(1, "José Núñez", "JN");
    CHECK(accountLabel(db.loadAccounts(1)[0]) == "Chase Checking 1234 (JN)");

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
    CHECK(accountLabel(account) == "BofA Checking 1234 (John Smith)");
    SortOptions byAccount;
    byAccount.byAccount = true;
    CHECK(subfolderFor(account, Quarter{2026, 1}, byAccount) == fs::path("BofA Checking 1234 (John Smith)"));

    // Blank display name = the full institution.
    s.db.updateAccount({id, s.caseId, "Bank of America", "  ", "Checking", "1234", {s.john}});
    CHECK(s.db.getAccount(id)->institutionDisplay == "Bank of America");
    CHECK(accountLabel(s.db.loadAccounts(s.caseId)[0]) == "Bank of America Checking 1234 (John Smith)");
}

TEST_CASE("Institution suggestions remember the latest abbreviation for each bank")
{
    SmithCase s;
    s.db.createAccount({0, s.caseId, "Bank of America", "BOA", "Checking", "1", {}});
    s.db.createAccount({0, s.caseId, "bank of america", "BofA", "Savings", "2", {}});  // newer
    s.db.createAccount({0, s.caseId, "Charles Schwab", "", "Brokerage", "3", {}});

    const auto other = s.db.createCase({0, "Other", ""});
    s.db.createAccount({0, other, "Ally", "", "Savings", "4", {}});  // other cases count too

    const auto names = s.db.institutionSuggestions();
    REQUIRE(names.size() == 3);
    CHECK(names[0].institution == "Ally");
    CHECK(names[1].institution == "bank of america");
    CHECK(names[1].displayName == "BofA");
    CHECK(names[2].displayName == "Charles Schwab");
}

TEST_CASE("Previous numbers are stored, validated and loaded")
{
    SmithCase s;
    const auto id = s.db.createAccount({0, s.caseId, "Chase", "", "Credit Card", "9012", {s.john}, {"5678", "1234"}});
    CHECK(s.db.getAccount(id)->previousLastFour == std::vector<std::string>{"5678", "1234"});
    CHECK(s.db.loadAccounts(s.caseId)[0].previousLastFour == std::vector<std::string>{"5678", "1234"});
    CHECK(accountFolderLabel(s.db.loadAccounts(s.caseId)[0]) ==
          "Chase Credit Card 9012 (was x5678, x1234) (John Smith)");

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
    CHECK(old[0].name == "Chsae Checking 1234 (John Smith)");
    CHECK(old[0].lastFour == "1234");

    // Card replaced: 1234 becomes a previous number. File names for 1234 stay
    // valid, so only the folder name is new history.
    s.db.updateAccount({id, s.caseId, "Chase", "", "Checking", "5678", {s.john}, {"1234"}});
    old = s.db.oldAccountNames(s.caseId);
    REQUIRE(old.size() == 3);
    CHECK(old[2].isFolder);
    CHECK(old[2].name == "Chase Checking 1234 (John Smith)");

    // A person's new display name changes the names of their accounts.
    s.db.updatePerson(s.john, "John Smith", "H");
    old = s.db.oldAccountNames(s.caseId);
    bool found = false;
    for (const auto& o : old)
        if (!o.isFolder && o.name == "Chase Checking 5678 (John Smith)") found = true;
    CHECK(found);

    // Saving without changes records nothing new.
    const auto count = s.db.oldAccountNames(s.caseId).size();
    s.db.updateAccount(*s.db.getAccount(id));
    CHECK(s.db.oldAccountNames(s.caseId).size() == count);
}

// ---- Combined statements ---------------------------------------------------

TEST_CASE("Combined statements are saved and named from their accounts")
{
    SmithCase s;
    const auto chk = s.db.createAccount({0, s.caseId, "JPMorgan Chase", "Chase", "Chk", "1111", {s.john}});
    const auto sav = s.db.createAccount({0, s.caseId, "JPMorgan Chase", "Chase", "Sav", "2222", {s.jane}});
    const auto chk2 = s.addAccount("JPMorgan Chase", "Chk", "3333", {});
    s.addAccount("Ally", "Savings", "9999", {});

    AccountRecord c;
    c.caseId = s.caseId;
    c.ownerIds = {s.jane, s.john};
    c.memberIds = {chk, sav, chk2};
    const auto id = s.db.createAccount(c);

    const auto rec = s.db.getAccount(id);
    REQUIRE(rec);
    CHECK(rec->isCombined());
    CHECK(rec->memberIds == std::vector<std::int64_t>{chk, sav, chk2});
    CHECK(rec->institution == "JPMorgan Chase");  // from its accounts, for display
    CHECK(rec->ownerIds == std::vector<std::int64_t>{s.jane, s.john});

    const auto accounts = s.db.loadAccounts(s.caseId);
    REQUIRE(accounts.size() == 5);
    CHECK(accountLabel(accounts[0]) == "Ally Savings 9999");
    const Account& combined = accounts.back();  // after the institution's own accounts
    REQUIRE(combined.isCombined());
    CHECK(accountLabel(combined) == "Chase Chk x1111, Sav x2222, Chk x3333 (Jane Smith; John Smith)");
    CHECK(accountFolderLabel(combined) == accountLabel(combined));

    // Reorder and drop one.
    c.id = id;
    c.memberIds = {sav, chk};
    c.ownerIds = {s.john};
    s.db.updateAccount(c);
    CHECK(accountLabel(s.db.loadAccounts(s.caseId).back()) == "Chase Sav x2222, Chk x1111 (John Smith)");

    // Combined statements don't feed the type/institution suggestions.
    for (const auto& t : s.db.accountTypeSuggestions()) CHECK_FALSE(t.empty());
    for (const auto& i : s.db.institutionSuggestions()) CHECK_FALSE(i.institution.empty());
}

TEST_CASE("Combined statement rules")
{
    SmithCase s;
    const auto chk = s.addAccount("Chase", "Chk", "1111", {});
    const auto sav = s.addAccount("chase", "Sav", "2222", {});  // same institution, any case
    const auto ally = s.addAccount("Ally", "Savings", "9999", {});

    AccountRecord c;
    c.caseId = s.caseId;
    c.memberIds = {chk};
    CHECK_THROWS_WITH(s.db.createAccount(c), ContainsSubstring("at least two"));
    c.memberIds = {chk, chk};
    CHECK_THROWS_WITH(s.db.createAccount(c), ContainsSubstring("listed twice"));
    c.memberIds = {chk, ally};
    CHECK_THROWS_WITH(s.db.createAccount(c), ContainsSubstring("same institution"));

    const auto other = s.db.createCase({0, "Other", ""});
    const auto foreign = s.db.createAccount({0, other, "Chase", "", "Chk", "4444", {}});
    c.memberIds = {chk, foreign};
    CHECK_THROWS_WITH(s.db.createAccount(c), ContainsSubstring("different case"));

    c.memberIds = {chk, sav};
    const auto id = s.db.createAccount(c);
    c.memberIds = {id, chk};
    CHECK_THROWS_WITH(s.db.createAccount(c), ContainsSubstring("another combined statement"));

    // A single account can't become a combined statement or the reverse.
    AccountRecord single = *s.db.getAccount(chk);
    single.memberIds = {sav, ally};
    CHECK_THROWS_WITH(s.db.updateAccount(single), ContainsSubstring("can't be changed"));

    // An account on a statement can't be deleted; the statement can.
    CHECK_THROWS_WITH(s.db.deleteAccount(sav), ContainsSubstring("Chase Chk x1111, Sav x2222"));
    s.db.deleteAccount(id);
    s.db.deleteAccount(sav);
    CHECK(s.db.loadAccounts(s.caseId).size() == 2);

    // Deleting the whole case is fine.
    c.memberIds = {chk, s.addAccount("Chase", "Sav", "5555", {})};
    s.db.createAccount(c);
    s.db.deleteCase(s.caseId);
    CHECK(s.db.listAccountRecords(s.caseId).empty());
}

TEST_CASE("Editing an account records old names of its combined statements")
{
    SmithCase s;
    const auto chk = s.addAccount("Chase", "Chekcing", "1111", {});
    const auto sav = s.addAccount("Chase", "Savings", "2222", {});
    AccountRecord c;
    c.caseId = s.caseId;
    c.ownerIds = {s.john};
    c.memberIds = {chk, sav};
    const auto id = s.db.createAccount(c);

    auto namesOf = [&](std::int64_t accountId) {
        std::vector<std::pair<bool, std::string>> out;
        for (const auto& o : s.db.oldAccountNames(s.caseId))
            if (o.accountId == accountId) out.push_back({o.isFolder, o.name});
        return out;
    };

    // A typo in a member's type: files and the folder were named the old way.
    AccountRecord r = *s.db.getAccount(chk);
    r.accountType = "Checking";
    s.db.updateAccount(r);
    const std::vector<std::pair<bool, std::string>> typo{
        {false, "Chase Chekcing x1111, Savings x2222 (John Smith)"},
        {true, "Chase Chekcing x1111, Savings x2222 (John Smith)"}};
    CHECK(namesOf(id) == typo);

    // The savings account gets a newer number: older statements still show
    // 2222 and keep their names; only the folder takes the new name.
    r = *s.db.getAccount(sav);
    r.previousLastFour = {r.lastFour};
    r.lastFour = "7777";
    s.db.updateAccount(r);
    auto names = namesOf(id);
    REQUIRE(names.size() == 3);
    CHECK(names[2] == std::pair<bool, std::string>{true, "Chase Checking x1111, Savings x2222 (John Smith)"});

    // Renaming an owner of the statement.
    s.db.updatePerson(s.john, "John Smith", "H");
    names = namesOf(id);
    REQUIRE(names.size() == 5);
    CHECK(names[3].second == "Chase Checking x1111, Savings x7777 (John Smith)");
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
            DROP TABLE combined_members;
            ALTER TABLE accounts DROP COLUMN is_combined;
            ALTER TABLE accounts DROP COLUMN opened_on;
            ALTER TABLE accounts DROP COLUMN closed_on;
            INSERT INTO cases (id, client_name) VALUES (1, 'Old Case');
            INSERT INTO accounts (id, case_id, institution, account_type, last_four, institution_display)
                VALUES (1, 1, 'Chase', 'Checking', '1234', 'Chase');
            PRAGMA user_version = 4;
        )sql");
    }
    Database db(file);
    CHECK(db.schemaVersion() == 6);
    REQUIRE(db.loadAccounts(1).size() == 1);
    CHECK_FALSE(db.loadAccounts(1)[0].isCombined());
    const auto sav = db.createAccount({0, 1, "Chase", "", "Savings", "5678", {}});
    AccountRecord c;
    c.caseId = 1;
    c.memberIds = {1, sav};
    db.createAccount(c);
    CHECK(accountLabel(db.loadAccounts(1).back()) == "Chase Checking x1234, Savings x5678");
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
            ALTER TABLE accounts DROP COLUMN opened_on;
            ALTER TABLE accounts DROP COLUMN closed_on;
            INSERT INTO cases (id, client_name) VALUES (1, 'Old Case');
            INSERT INTO accounts (id, case_id, institution, account_type, last_four, institution_display, is_combined)
                VALUES (1, 1, 'Chase', 'Checking', '1234', 'Chase', 0);
            PRAGMA user_version = 5;
        )sql");
    }
    Database db(file);
    CHECK(db.schemaVersion() == 6);
    auto r = *db.getAccount(1);
    CHECK_FALSE(r.openedOn);
    CHECK_FALSE(r.closedOn);
    r.openedOn = makeDate(2020, 6, 1);
    db.updateAccount(r);
    CHECK(db.getAccount(1)->openedOn == makeDate(2020, 6, 1));
}

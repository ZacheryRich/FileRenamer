#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "TestHelpers.h"
#include "finrenamer/Database.h"
#include "finrenamer/FilenameBuilder.h"

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
        return db.createAccount({0, caseId, inst, type, four, owners});
    }
};

}  // namespace

TEST_CASE("A new database is created at the current schema version")
{
    testing::TempDir dir;
    const fs::path file = dir.path() / "finrenamer.db";
    {
        Database db(file);
        CHECK(db.schemaVersion() == 1);
        db.createCase({0, "Persisted", ""});
    }
    CHECK(fs::exists(file));

    Database reopened(file);  // reopening must not re-run migrations
    CHECK(reopened.schemaVersion() == 1);
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

    s.db.renamePerson(s.john, "John Q. Smith");
    CHECK_THROWS_AS(s.db.renamePerson(s.john, "JANE SMITH"), DatabaseError);
    CHECK_NOTHROW(s.db.renamePerson(s.john, "john q. smith"));  // own name, new casing

    const auto people = s.db.listPeople(s.caseId);
    REQUIRE(people.size() == 2);
    CHECK(people[0].fullName == "Jane Smith");
    CHECK(people[1].fullName == "john q. smith");
}

TEST_CASE("Accounts keep owners in display order")
{
    SmithCase s;
    const auto id = s.addAccount("Chase", "Checking", "1234", {s.john, s.jane});

    auto accounts = s.db.loadAccounts(s.caseId);
    REQUIRE(accounts.size() == 1);
    CHECK(accounts[0].owners == std::vector<std::string>{"John Smith", "Jane Smith"});
    CHECK(accountLabel(accounts[0]) == "Chase Checking 1234 (John Smith; Jane Smith)");

    s.db.updateAccount({id, s.caseId, "Chase", "Checking", "1234", {s.jane, s.john}});
    accounts = s.db.loadAccounts(s.caseId);
    CHECK(accounts[0].owners == std::vector<std::string>{"Jane Smith", "John Smith"});

    s.db.updateAccount({id, s.caseId, "Chase", "Checking", "1234", {}});
    CHECK(s.db.loadAccounts(s.caseId)[0].owners.empty());
}

TEST_CASE("Renaming a person updates every account they own")
{
    SmithCase s;
    s.addAccount("Chase", "Checking", "1234", {s.john, s.jane});
    s.addAccount("Fidelity", "Brokerage", "5678", {s.john});

    s.db.renamePerson(s.john, "Jonathan Smith");
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
        db.createAccount({0, caseId, "Banco Popular", "Ahorros", "9876", {jose}});
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

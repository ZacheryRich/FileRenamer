#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "finrenamer/Database.h"
#include "finrenamer/NameFixer.h"
#include "finrenamer/RenameEngine.h"

using namespace finrenamer;
namespace fs = std::filesystem;

namespace {

// A case with one account, edited through the database so old names are recorded.
struct Fixture {
    testing::TempDir dir;
    Database db = Database::openInMemory();
    std::int64_t caseId = db.createCase({0, "Smith", ""}, defaultCasePeople());
    std::int64_t husband = db.listPeople(caseId)[0].id;
    std::int64_t accountId = 0;

    RenamePlan plan(const NameFixOptions& options = {})
    {
        return planNameFixes({dir.path()}, db.loadAccounts(caseId), db.oldAccountNames(caseId), options);
    }

    // Plans and executes like the GUI does; returns the batch.
    BatchRecord fix(const NameFixOptions& options = {})
    {
        const auto result = execute(plan(options));
        REQUIRE(result.failures.empty());
        return result.record;
    }
};

}  // namespace

TEST_CASE("Fixing a typo renames the files that used it, wherever they are")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Chsae", "", "Checking", "1234", {f.husband}});
    f.dir.touch("2026.01.31 Chsae Checking 1234 (H).pdf", "JAN");
    f.dir.touch("2026/2026.Q1 Chsae Checking 1234 (H).pdf", "Q1");
    f.dir.touch("2026.02.28 Chsae Checking 1234 (H) (2).pdf", "FEB2");
    f.dir.touch("2026.03.31 Other Bank Checking 1234 (H).pdf", "OTHER");   // not ours
    f.dir.touch("scan0001.pdf", "UNNAMED");                                // not renamed yet
    f.dir.touch("notes.txt", "TXT");

    f.db.updateAccount({f.accountId, f.caseId, "Chase", "", "Checking", "1234", {f.husband}});
    const auto plan = f.plan();
    CHECK(plan.moves.size() == 3);

    f.fix();
    CHECK(testing::readFile(f.dir.path() / "2026.01.31 Chase Checking 1234 (H).pdf") == "JAN");
    CHECK(testing::readFile(f.dir.path() / "2026" / "2026.Q1 Chase Checking 1234 (H).pdf") == "Q1");
    CHECK(testing::readFile(f.dir.path() / "2026.02.28 Chase Checking 1234 (H).pdf") == "FEB2");
    CHECK(fs::exists(f.dir.path() / "2026.03.31 Other Bank Checking 1234 (H).pdf"));
    CHECK(fs::exists(f.dir.path() / "scan0001.pdf"));
    CHECK(fs::exists(f.dir.path() / "notes.txt"));

    CHECK(f.plan().moves.empty());  // running it again finds nothing
}

TEST_CASE("A replaced card: old statements keep the old number, the folder gets (was ...)")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Chase", "", "Credit Card", "1234", {f.husband}});
    f.dir.touch("Chase Credit Card 1234 (H)/2025/2025.12.31 Chase Credit Card 1234 (H).pdf", "DEC");
    f.dir.touch("Chase Credit Card 1234 (H)/2025/notes.txt", "NOTES");

    f.db.updateAccount({f.accountId, f.caseId, "Chase", "", "Credit Card", "5678", {f.husband}, {"1234"}});
    const auto batch = f.fix();

    const fs::path folder = f.dir.path() / "Chase Credit Card 5678 (was x1234) (H)";
    CHECK(testing::readFile(folder / "2025" / "2025.12.31 Chase Credit Card 1234 (H).pdf") == "DEC");
    CHECK(testing::readFile(folder / "2025" / "notes.txt") == "NOTES");  // other files move with the folder
    CHECK_FALSE(fs::exists(f.dir.path() / "Chase Credit Card 1234 (H)"));  // renamed, not copied

    // Undo puts everything back, including the removed folder.
    const auto undone = undo(batch);
    CHECK(undone.failures.empty());
    CHECK(testing::readFile(f.dir.path() / "Chase Credit Card 1234 (H)" / "2025" /
                            "2025.12.31 Chase Credit Card 1234 (H).pdf") == "DEC");
    CHECK_FALSE(fs::exists(folder));
}

TEST_CASE("A mistyped number that isn't kept is corrected in file names")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Chase", "", "Checking", "1243", {f.husband}});
    f.dir.touch("2026.01.31 Chase Checking 1243 (H).pdf");

    f.db.updateAccount({f.accountId, f.caseId, "Chase", "", "Checking", "1234", {f.husband}});  // typo, not kept
    f.fix();
    CHECK(fs::exists(f.dir.path() / "2026.01.31 Chase Checking 1234 (H).pdf"));
}

TEST_CASE("Folders named before previous numbers existed are renamed in place")
{
    Fixture f;
    // Previous number entered from the start: no history, but a folder named
    // with either number is still recognised as this account's folder.
    f.accountId = f.db.createAccount({0, f.caseId, "Amex", "", "Credit Card", "5678", {f.husband}, {"1234"}});
    f.dir.touch("2024/Amex Credit Card 1234 (H)/2024.06.30 Amex Credit Card 1234 (H).pdf", "OLD");

    f.fix();
    // Renamed where it is (inside 2024), contents untouched.
    CHECK(testing::readFile(f.dir.path() / "2024" / "Amex Credit Card 5678 (was x1234) (H)" /
                            "2024.06.30 Amex Credit Card 1234 (H).pdf") == "OLD");
    CHECK_FALSE(fs::exists(f.dir.path() / "2024" / "Amex Credit Card 1234 (H)"));
}

TEST_CASE("A folder is never merged into another: if the new name exists, it's left alone")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Amex", "", "Credit Card", "5678", {f.husband}, {"1234"}});
    f.dir.touch("Amex Credit Card 1234 (H)/2024.06.30 Amex Credit Card 1234 (H).pdf", "OLD");
    f.dir.touch("Amex Credit Card 5678 (was x1234) (H)/2026.01.31 Amex Credit Card 5678 (H).pdf", "NEW");

    const auto plan = f.plan();
    REQUIRE(plan.moves.size() == 1);
    CHECK(plan.moves[0].status == MoveStatus::Collision);
    CHECK(plan.moves[0].message == kFolderRenameTag);

    f.fix();
    CHECK(testing::readFile(f.dir.path() / "Amex Credit Card 1234 (H)" / "2024.06.30 Amex Credit Card 1234 (H).pdf") == "OLD");
    CHECK(testing::readFile(f.dir.path() / "Amex Credit Card 5678 (was x1234) (H)" / "2026.01.31 Amex Credit Card 5678 (H).pdf") == "NEW");
}

TEST_CASE("Several folders from different sessions: each file is renamed where it is")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Chsae", "", "Checking", "1234", {f.husband}});
    f.dir.touch("Session A/2026.01.31 Chsae Checking 1234 (H).pdf", "A");
    f.dir.touch("Session B/2026.02.28 Chsae Checking 1234 (H).pdf", "B");
    f.dir.touch("Session B/deeper/2026.03.31 Chsae Checking 1234 (H).pdf", "B2");
    f.dir.touch("Not chosen/2026.04.30 Chsae Checking 1234 (H).pdf", "X");
    f.db.updateAccount({f.accountId, f.caseId, "Chase", "", "Checking", "1234", {f.husband}});

    const auto accounts = f.db.loadAccounts(f.caseId);
    const auto old = f.db.oldAccountNames(f.caseId);
    const fs::path a = f.dir.path() / "Session A", b = f.dir.path() / "Session B";

    // Same folder listed twice, and a folder inside another one: no duplicates.
    auto plan = planNameFixes({a, b, b, b / "deeper"}, accounts, old);
    CHECK(plan.moves.size() == 3);
    CHECK(plan.root == f.dir.path());

    // Without subfolders, "deeper" is only searched if it's listed itself.
    NameFixOptions topOnly;
    topOnly.includeSubfolders = false;
    CHECK(planNameFixes({a, b}, accounts, old, topOnly).moves.size() == 2);
    CHECK(planNameFixes({a, b, b / "deeper"}, accounts, old, topOnly).moves.size() == 3);

    REQUIRE(execute(plan).failures.empty());
    CHECK(testing::readFile(a / "2026.01.31 Chase Checking 1234 (H).pdf") == "A");
    CHECK(testing::readFile(b / "2026.02.28 Chase Checking 1234 (H).pdf") == "B");
    CHECK(testing::readFile(b / "deeper" / "2026.03.31 Chase Checking 1234 (H).pdf") == "B2");
    CHECK(fs::exists(f.dir.path() / "Not chosen" / "2026.04.30 Chsae Checking 1234 (H).pdf"));
}

TEST_CASE("Folder renaming can be turned off")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Chsae", "", "Checking", "1234", {f.husband}});
    f.dir.touch("Chsae Checking 1234 (H)/2026.01.31 Chsae Checking 1234 (H).pdf");
    f.db.updateAccount({f.accountId, f.caseId, "Chase", "", "Checking", "1234", {f.husband}});

    NameFixOptions filesOnly;
    filesOnly.renameFolders = false;
    f.fix(filesOnly);
    CHECK(fs::exists(f.dir.path() / "Chsae Checking 1234 (H)" / "2026.01.31 Chase Checking 1234 (H).pdf"));
}

TEST_CASE("A chosen folder that is itself an account folder is renamed too")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Chsae", "", "Checking", "1234", {f.husband}});
    const fs::path chosen = f.dir.path() / "Chsae Checking 1234 (H)";
    f.dir.touch("Chsae Checking 1234 (H)/2026.01.31 Chsae Checking 1234 (H).pdf", "IN");
    f.db.updateAccount({f.accountId, f.caseId, "Chase", "", "Checking", "1234", {f.husband}});

    const auto plan = planNameFixes({chosen}, f.db.loadAccounts(f.caseId), f.db.oldAccountNames(f.caseId));
    REQUIRE(execute(plan).failures.empty());
    CHECK(testing::readFile(f.dir.path() / "Chase Checking 1234 (H)" / "2026.01.31 Chase Checking 1234 (H).pdf") == "IN");
}

TEST_CASE("Renaming never overwrites: clashes get a number")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Chsae", "", "Checking", "1234", {f.husband}});
    f.dir.touch("2026.01.31 Chsae Checking 1234 (H).pdf", "OLD NAME");
    f.dir.touch("2026.01.31 Chase Checking 1234 (H).pdf", "ALREADY THERE");

    f.db.updateAccount({f.accountId, f.caseId, "Chase", "", "Checking", "1234", {f.husband}});
    f.fix();
    CHECK(testing::readFile(f.dir.path() / "2026.01.31 Chase Checking 1234 (H).pdf") == "ALREADY THERE");
    CHECK(testing::readFile(f.dir.path() / "2026.01.31 Chase Checking 1234 (H) (2).pdf") == "OLD NAME");
}

TEST_CASE("A new owner display name updates file and folder names")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Chase", "", "Checking", "1234", {f.husband}});
    f.dir.touch("Chase Checking 1234 (H)/2026.01.31 Chase Checking 1234 (H).pdf");

    f.db.updatePerson(f.husband, "John Smith", "JS");
    f.fix();
    CHECK(fs::exists(f.dir.path() / "Chase Checking 1234 (JS)" / "2026.01.31 Chase Checking 1234 (JS).pdf"));
}

TEST_CASE("Letter-case-only fixes are applied")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "chase", "", "checking", "1234", {f.husband}});
    f.dir.touch("2026.01.31 chase checking 1234 (H).pdf");
    f.db.updateAccount({f.accountId, f.caseId, "Chase", "", "Checking", "1234", {f.husband}});
    f.fix();
    bool found = false;
    for (const auto& e : fs::directory_iterator(f.dir.path()))
        if (e.path().filename() == "2026.01.31 Chase Checking 1234 (H).pdf") found = true;
    CHECK(found);
}

TEST_CASE("Correcting a typo in an older number gives its files the corrected number")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Chase", "", "Credit Card", "9012", {f.husband}, {"5687", "1234"}});
    f.dir.touch("2024.06.30 Chase Credit Card 5687 (H).pdf");   // named with the typo
    f.dir.touch("2023.06.30 Chase Credit Card 1234 (H).pdf");   // fine

    // 5687 was a typo for 5678 (not the current number).
    f.db.updateAccount({f.accountId, f.caseId, "Chase", "", "Credit Card", "9012", {f.husband}, {"5678", "1234"}},
                       {{"5687", "5678"}});
    f.fix();
    CHECK(fs::exists(f.dir.path() / "2024.06.30 Chase Credit Card 5678 (H).pdf"));
    CHECK(fs::exists(f.dir.path() / "2023.06.30 Chase Credit Card 1234 (H).pdf"));
}

TEST_CASE("Reordering numbers renames the account folder only")
{
    Fixture f;
    f.accountId = f.db.createAccount({0, f.caseId, "Amex", "", "Card", "1234", {f.husband}, {"5678"}});
    f.dir.touch("Amex Card 1234 (was x5678) (H)/2026.01.31 Amex Card 1234 (H).pdf");

    // Oops: 5678 is actually the newer number.
    f.db.updateAccount({f.accountId, f.caseId, "Amex", "", "Card", "5678", {f.husband}, {"1234"}});
    f.fix();
    CHECK(fs::exists(f.dir.path() / "Amex Card 5678 (was x1234) (H)" / "2026.01.31 Amex Card 1234 (H).pdf"));
}

TEST_CASE("Combined statement files and folders are fixed after a member's typo")
{
    Fixture f;
    const auto chk = f.db.createAccount({0, f.caseId, "Chase", "", "Chekcing", "1111", {}});
    const auto sav = f.db.createAccount({0, f.caseId, "Chase", "", "Savings", "2222", {}});
    AccountRecord c;
    c.caseId = f.caseId;
    c.ownerIds = {f.husband};
    c.memberIds = {chk, sav};
    f.db.createAccount(c);

    const fs::path folder = f.dir.path() / "Chase Chekcing x1111, Savings x2222 (H)";
    f.dir.touch("Chase Chekcing x1111, Savings x2222 (H)/2026.01.31 Chase Chekcing x1111, Savings x2222 (H).pdf", "jan");

    AccountRecord r = *f.db.getAccount(chk);
    r.accountType = "Checking";
    f.db.updateAccount(r);
    f.fix();

    const fs::path fixed = f.dir.path() / "Chase Checking x1111, Savings x2222 (H)";
    CHECK(testing::readFile(fixed / "2026.01.31 Chase Checking x1111, Savings x2222 (H).pdf") == "jan");
    CHECK_FALSE(fs::exists(folder));
}

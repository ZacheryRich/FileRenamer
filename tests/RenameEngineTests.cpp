#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "finrenamer/RenameEngine.h"

using namespace finrenamer;
namespace fs = std::filesystem;

TEST_CASE("Apply renames and sorts, then undo restores everything")
{
    testing::TempDir dir;
    const fs::path a = dir.touch("scan1.pdf", "AAA");
    const fs::path b = dir.touch("scan2.pdf", "BBB");

    PlanOptions opts;
    opts.sort.byAccount = true;
    opts.sort.byYear = true;
    const auto plan = buildPlan(dir.path(), {
        PlanInput{a, testing::chaseJoint(), SingleDate{makeDate(2026, 1, 31)}},
        PlanInput{b, testing::fidelitySingle(), Quarter{2025, 4}},
    }, opts);

    const auto result = execute(plan);
    REQUIRE(result.failures.empty());
    REQUIRE(result.record.moves.size() == 2);

    const fs::path aDest = dir.path() / "Chase Checking 1234 (John Smith; Jane Smith)" / "2026" /
                           "2026.01.31 Chase Checking 1234 (John Smith; Jane Smith).pdf";
    const fs::path bDest = dir.path() / "Fidelity Brokerage 5678 (Jane Smith)" / "2025" /
                           "2025.Q4 Fidelity Brokerage 5678 (Jane Smith).pdf";
    CHECK(testing::readFile(aDest) == "AAA");
    CHECK(testing::readFile(bDest) == "BBB");
    CHECK_FALSE(fs::exists(a));
    CHECK(result.record.createdFolders.size() == 4);

    const auto undone = undo(result.record);
    CHECK(undone.failures.empty());
    CHECK(undone.restored == 2);
    CHECK(undone.foldersRemoved == 4);
    CHECK(testing::readFile(a) == "AAA");
    CHECK(testing::readFile(b) == "BBB");
    CHECK_FALSE(fs::exists(dir.path() / "Chase Checking 1234 (John Smith; Jane Smith)"));
}

TEST_CASE("Undo never deletes folders that existed before, or that hold other files")
{
    testing::TempDir dir;
    dir.touch("2026/older statement.pdf");  // 2026 already exists
    const fs::path a = dir.touch("scan1.pdf");
    const fs::path b = dir.touch("scan2.pdf");

    PlanOptions opts;
    opts.sort.byYear = true;
    const auto plan = buildPlan(dir.path(), {
        PlanInput{a, testing::chaseJoint(), SingleDate{makeDate(2026, 1, 31)}},
        PlanInput{b, testing::chaseJoint(), SingleDate{makeDate(2025, 6, 30)}},
    }, opts);
    const auto result = execute(plan);
    REQUIRE(result.failures.empty());
    REQUIRE(result.record.createdFolders.size() == 1);  // only 2025 was new

    dir.touch("2025/added by hand.pdf");  // user drops a file in afterwards

    const auto undone = undo(result.record);
    CHECK(undone.restored == 2);
    CHECK(undone.foldersRemoved == 0);
    CHECK(fs::exists(dir.path() / "2026" / "older statement.pdf"));
    CHECK(fs::exists(dir.path() / "2025" / "added by hand.pdf"));
}

TEST_CASE("A file that appears after planning is not overwritten")
{
    testing::TempDir dir;
    const fs::path a = dir.touch("scan1.pdf", "MINE");
    const auto plan = buildPlan(dir.path(), {
        PlanInput{a, testing::fidelitySingle(), Quarter{2026, 1}}});

    const fs::path dest = plan.moves[0].destination;
    dir.touch(utf8FromPath(dest.filename()), "SOMEONE ELSE");

    const auto result = execute(plan);
    CHECK(result.failures.size() == 1);
    CHECK(result.record.moves.empty());
    CHECK(testing::readFile(dest) == "SOMEONE ELSE");
    CHECK(testing::readFile(a) == "MINE");
}

TEST_CASE("Failed moves do not leave empty folders behind")
{
    testing::TempDir dir;
    const fs::path a = dir.touch("scan1.pdf");
    PlanOptions opts;
    opts.sort.byAccount = true;
    const auto plan = buildPlan(dir.path(), {
        PlanInput{a, testing::fidelitySingle(), Quarter{2026, 1}}}, opts);

    fs::remove(a);  // file vanishes (or is locked) before Apply
    const auto result = execute(plan);
    CHECK(result.failures.size() == 1);
    CHECK(result.record.createdFolders.empty());
    CHECK_FALSE(fs::exists(dir.path() / "Fidelity Brokerage 5678 (Jane Smith)"));
}

TEST_CASE("Non-ASCII names survive the round trip")
{
    testing::TempDir dir;
    const fs::path a = dir.touch("estado de cuenta.pdf");

    Account acct = testing::chaseJoint();
    acct.institution = "Banco Popular";
    acct.owners = {"José Núñez", "María Peña"};

    const auto plan = buildPlan(dir.path(), {PlanInput{a, acct, SingleDate{makeDate(2026, 3, 31)}}});
    const auto result = execute(plan);
    REQUIRE(result.failures.empty());
    CHECK(fs::exists(dir.path() /
                     pathFromUtf8("2026.03.31 Banco Popular Checking 1234 (José Núñez; María Peña).pdf")));
}

TEST_CASE("Undo reports files that were moved again since")
{
    testing::TempDir dir;
    const fs::path a = dir.touch("scan1.pdf");
    const auto plan = buildPlan(dir.path(), {PlanInput{a, testing::fidelitySingle(), Quarter{2026, 1}}});
    const auto result = execute(plan);
    REQUIRE(result.record.moves.size() == 1);

    fs::rename(result.record.moves[0].to, dir.path() / "renamed by hand.pdf");
    const auto undone = undo(result.record);
    CHECK(undone.restored == 0);
    CHECK(undone.failures.size() == 1);
}

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "finrenamer/FileScanner.h"
#include "finrenamer/RenameEngine.h"
#include "finrenamer/RenamePlan.h"

using namespace finrenamer;
namespace fs = std::filesystem;

namespace {
PlanInput input(const fs::path& src, const Account& a, const DateSpec& d)
{
    return PlanInput{src, a, d};
}
}  // namespace

TEST_CASE("Scanner finds only top-level PDFs, any case, sorted")
{
    testing::TempDir dir;
    dir.touch("b.pdf");
    dir.touch("A.PDF");
    dir.touch("notes.txt");
    dir.touch("sub/inner.pdf");

    const auto files = listPdfFiles(dir.path());
    REQUIRE(files.size() == 2);
    CHECK(files[0].filename() == "A.PDF");
    CHECK(files[1].filename() == "b.pdf");
}

TEST_CASE("A simple rename is Ready with the expected destination")
{
    testing::TempDir dir;
    const fs::path src = dir.touch("scan001.pdf");

    const auto plan = buildPlan(dir.path(), {input(src, testing::chaseJoint(),
                                                   SingleDate{makeDate(2026, 1, 31)})});
    REQUIRE(plan.moves.size() == 1);
    CHECK(plan.moves[0].status == MoveStatus::Ready);
    CHECK(plan.moves[0].destination ==
          dir.path() / "2026.01.31 Chase Checking x1234 (John Smith; Jane Smith).pdf");
    CHECK(plan.hasWork());
    CHECK(fs::exists(src));  // planning never touches the disk
}

TEST_CASE("Files without an account or date are Incomplete")
{
    testing::TempDir dir;
    const fs::path src = dir.touch("scan001.pdf");
    const auto plan = buildPlan(dir.path(), {PlanInput{src, testing::chaseJoint(), std::nullopt}});
    CHECK(plan.moves[0].status == MoveStatus::Incomplete);
    CHECK_FALSE(plan.hasWork());
}

TEST_CASE("Two files with the same name get a numbered suffix")
{
    testing::TempDir dir;
    const fs::path a = dir.touch("a.pdf");
    const fs::path b = dir.touch("b.pdf");
    const DateSpec d = SingleDate{makeDate(2026, 1, 31)};

    const auto plan = buildPlan(dir.path(), {input(a, testing::chaseJoint(), d),
                                             input(b, testing::chaseJoint(), d)});
    CHECK(plan.moves[0].destination.filename() ==
          "2026.01.31 Chase Checking x1234 (John Smith; Jane Smith).pdf");
    CHECK(plan.moves[1].destination.filename() ==
          "2026.01.31 Chase Checking x1234 (John Smith; Jane Smith) (2).pdf");
    CHECK(plan.moves[1].status == MoveStatus::Ready);
    CHECK_FALSE(plan.moves[1].message.empty());
}

TEST_CASE("An existing file on disk is never overwritten")
{
    testing::TempDir dir;
    dir.touch("2026.Q1 Fidelity Brokerage x5678 (Jane Smith).pdf");
    const fs::path src = dir.touch("new.pdf");

    const auto plan = buildPlan(dir.path(), {input(src, testing::fidelitySingle(), Quarter{2026, 1})});
    CHECK(plan.moves[0].destination.filename() ==
          "2026.Q1 Fidelity Brokerage x5678 (Jane Smith) (2).pdf");
}

TEST_CASE("Collisions inside a subfolder are detected too")
{
    testing::TempDir dir;
    dir.touch("2026/2026.Q1 Fidelity Brokerage x5678 (Jane Smith).pdf");
    const fs::path src = dir.touch("new.pdf");

    PlanOptions opts;
    opts.sort.byYear = true;
    const auto plan = buildPlan(dir.path(), {input(src, testing::fidelitySingle(), Quarter{2026, 1})}, opts);
    CHECK(plan.moves[0].destination ==
          dir.path() / "2026" / "2026.Q1 Fidelity Brokerage x5678 (Jane Smith) (2).pdf");
}

TEST_CASE("Block policy marks collisions instead of numbering")
{
    testing::TempDir dir;
    const fs::path a = dir.touch("a.pdf");
    const fs::path b = dir.touch("b.pdf");
    const DateSpec d = Quarter{2026, 2};

    PlanOptions opts;
    opts.collisions = CollisionPolicy::Block;
    const auto plan = buildPlan(dir.path(), {input(a, testing::fidelitySingle(), d),
                                             input(b, testing::fidelitySingle(), d)}, opts);
    CHECK(plan.moves[0].status == MoveStatus::Ready);
    CHECK(plan.moves[1].status == MoveStatus::Collision);
}

TEST_CASE("A file that already has the right name is Unchanged")
{
    testing::TempDir dir;
    const fs::path src = dir.touch("2026.Q1 Fidelity Brokerage x5678 (Jane Smith).pdf");
    const auto plan = buildPlan(dir.path(), {input(src, testing::fidelitySingle(), Quarter{2026, 1})});
    CHECK(plan.moves[0].status == MoveStatus::Unchanged);
    CHECK_FALSE(plan.hasWork());
}

TEST_CASE("Invalid input is reported as an Error")
{
    testing::TempDir dir;
    testing::TempDir other;
    const fs::path src = dir.touch("a.pdf");
    const fs::path outside = other.touch("b.pdf");

    Account noInstitution = testing::chaseJoint();
    noInstitution.institution.clear();

    const auto plan = buildPlan(dir.path(), {
        input(src, testing::chaseJoint(), SingleDate{makeDate(2026, 2, 30)}),
        input(src, noInstitution, Quarter{2026, 1}),            // listed twice
        input(outside, testing::chaseJoint(), Quarter{2026, 1}),
        input(dir.path() / "missing.pdf", testing::chaseJoint(), Quarter{2026, 1}),
    });
    for (const auto& m : plan.moves) {
        CHECK(m.status == MoveStatus::Error);
        CHECK_FALSE(m.message.empty());
    }
}

TEST_CASE("Paths longer than the Windows limit are rejected")
{
    testing::TempDir dir;
    const fs::path src = dir.touch("a.pdf");

    PlanOptions opts;
    opts.maxPathLength = 40;  // simulate a deep folder
    const auto plan = buildPlan(dir.path(), {input(src, testing::chaseJoint(), Quarter{2026, 1})}, opts);
    CHECK(plan.moves[0].status == MoveStatus::Error);
}

TEST_CASE("Skipped files are left alone and still hold their name")
{
    testing::TempDir dir;
    // This file already uses the name the second file would get.
    const fs::path keep = dir.touch("2026.Q1 Fidelity Brokerage x5678 (Jane Smith).pdf", "KEEP");
    const fs::path other = dir.touch("scan.pdf");
    const fs::path cover = dir.touch("cover letter.pdf");

    const auto plan = buildPlan(dir.path(), {
        PlanInput{keep, std::nullopt, std::nullopt, /*skip=*/true},
        input(other, testing::fidelitySingle(), Quarter{2026, 1}),
        PlanInput{cover, testing::chaseJoint(), std::nullopt, /*skip=*/true},
    });

    CHECK(plan.moves[0].status == MoveStatus::Skipped);
    CHECK(plan.moves[0].destination.empty());
    CHECK(plan.moves[2].status == MoveStatus::Skipped);  // half-filled rows can be skipped too
    CHECK(plan.moves[1].destination.filename() ==
          "2026.Q1 Fidelity Brokerage x5678 (Jane Smith) (2).pdf");
    CHECK(plan.count(MoveStatus::Skipped) == 2);
    CHECK(plan.allResolved());

    const auto result = execute(plan);
    CHECK(result.record.moves.size() == 1);
    CHECK(testing::readFile(keep) == "KEEP");
    CHECK(fs::exists(cover));
}

TEST_CASE("allResolved is false while files are still incomplete")
{
    testing::TempDir dir;
    const fs::path a = dir.touch("a.pdf");
    const fs::path b = dir.touch("b.pdf");
    const auto plan = buildPlan(dir.path(), {
        input(a, testing::fidelitySingle(), Quarter{2026, 1}),
        PlanInput{b, std::nullopt, std::nullopt},
    });
    CHECK_FALSE(plan.allResolved());
}

TEST_CASE("Names that differ only in letter case are treated as the same")
{
    CHECK(caseFoldKey(pathFromUtf8("C:/Statements/JOSÉ NÚÑEZ.pdf")) ==
          caseFoldKey(pathFromUtf8("C:\\Statements\\josé núñez.PDF")));

    testing::TempDir dir;
    const fs::path a = dir.touch("a.pdf");
    const fs::path b = dir.touch("b.pdf");
    Account upper = testing::fidelitySingle();
    upper.owners = {"JOSÉ NÚÑEZ"};
    Account lower = testing::fidelitySingle();
    lower.owners = {"josé núñez"};

    const auto plan = buildPlan(dir.path(), {input(a, upper, Quarter{2026, 1}),
                                             input(b, lower, Quarter{2026, 1})});
    CHECK(utf8FromPath(plan.moves[1].destination.filename()) ==
          "2026.Q1 Fidelity Brokerage x5678 (josé núñez) (2).pdf");
}

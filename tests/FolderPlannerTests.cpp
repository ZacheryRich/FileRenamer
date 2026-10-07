#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "finrenamer/FolderPlanner.h"

using namespace finrenamer;
namespace fs = std::filesystem;

TEST_CASE("Subfolder layout for each sort option")
{
    const Account acct = testing::chaseJoint();
    const DateSpec date = SingleDate{makeDate(2026, 1, 31)};
    const fs::path accountDir = "Chase Checking 1234 (John Smith; Jane Smith)";

    SortOptions opts;
    CHECK(subfolderFor(acct, date, opts).empty());

    opts.byAccount = true;
    CHECK(subfolderFor(acct, date, opts) == accountDir);

    opts = {};
    opts.byYear = true;
    CHECK(subfolderFor(acct, date, opts) == fs::path("2026"));

    opts.byAccount = true;
    opts.order = FolderOrder::AccountThenYear;
    CHECK(subfolderFor(acct, date, opts) == accountDir / "2026");

    opts.order = FolderOrder::YearThenAccount;
    CHECK(subfolderFor(acct, date, opts) == fs::path("2026") / accountDir);
}

TEST_CASE("Year folders for periods use the end year")
{
    SortOptions opts;
    opts.byYear = true;
    const DateSpec p = Period{makeDate(2025, 12, 15), makeDate(2026, 1, 14)};
    CHECK(subfolderFor(testing::chaseJoint(), p, opts) == fs::path("2026"));
}

TEST_CASE("Account folders list previous numbers")
{
    Account a = testing::fidelitySingle();
    a.previousLastFour = {"1111"};
    SortOptions opts;
    opts.byAccount = true;
    CHECK(subfolderFor(a, Quarter{2026, 1}, opts) ==
          fs::path("Fidelity Brokerage 5678 (was x1111) (Jane Smith)"));
}

TEST_CASE("A combined statement gets its own account folder")
{
    Account c;
    c.institution = "Chase";
    c.combined = {{1, "Chk", "1111"}, {2, "Sav", "2222"}};
    c.owners = {"H"};
    SortOptions o;
    o.byAccount = true;
    o.byYear = true;
    CHECK(subfolderFor(c, SingleDate{makeDate(2026, 1, 31)}, o) ==
          std::filesystem::path("Chase Chk x1111, Sav x2222 (H)") / "2026");
}

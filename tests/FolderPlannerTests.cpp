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

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "finrenamer/RenameSession.h"

using namespace finrenamer;
namespace fs = std::filesystem;

// ---- Dates ---------------------------------------------------------------

TEST_CASE("nextDateSpec: single dates move one month, month-ends stay month-ends")
{
    auto next = [](int y, unsigned m, unsigned d) {
        return std::get<SingleDate>(nextDateSpec(SingleDate{makeDate(y, m, d)})).date;
    };
    CHECK(next(2026, 1, 15) == makeDate(2026, 2, 15));
    CHECK(next(2026, 1, 31) == makeDate(2026, 2, 28));
    CHECK(next(2026, 2, 28) == makeDate(2026, 3, 31));  // Feb 28 is month-end in 2026
    CHECK(next(2024, 2, 28) == makeDate(2024, 3, 28));  // ...but not in a leap year
    CHECK(next(2026, 1, 30) == makeDate(2026, 2, 28));  // clamped
    CHECK(next(2026, 12, 31) == makeDate(2027, 1, 31));
}

TEST_CASE("nextDateSpec: periods shift by their own length")
{
    auto next = [](Period p) { return std::get<Period>(nextDateSpec(p)); };

    auto m = next({makeDate(2026, 1, 1), makeDate(2026, 1, 31)});  // a calendar month
    CHECK(m.start == makeDate(2026, 2, 1));
    CHECK(m.end == makeDate(2026, 2, 28));

    auto q = next({makeDate(2026, 1, 1), makeDate(2026, 3, 31)});  // a calendar quarter
    CHECK(q.start == makeDate(2026, 4, 1));
    CHECK(q.end == makeDate(2026, 6, 30));

    auto cycle = next({makeDate(2025, 12, 15), makeDate(2026, 1, 14)});  // billing cycle
    CHECK(cycle.start == makeDate(2026, 1, 15));
    CHECK(cycle.end == makeDate(2026, 2, 14));
}

TEST_CASE("nextDateSpec: quarters roll over the year")
{
    auto q = std::get<Quarter>(nextDateSpec(Quarter{2025, 4}));
    CHECK(q.year == 2026);
    CHECK(q.quarter == 1);
    CHECK(std::get<Quarter>(nextDateSpec(Quarter{2026, 2})).quarter == 3);
}

TEST_CASE("parseUserDate accepts common US formats")
{
    const auto jan31 = makeDate(2026, 1, 31);
    for (const char* text : {"1/31/2026", "01/31/2026", "1-31-26", "01.31.2026", "01312026",
                             "013126", "2026-01-31", "2026.01.31", " 1/31/2026 "})
        CHECK(parseUserDate(text) == jan31);

    for (const char* text : {"", "2/30/2026", "13/01/2026", "1/31", "Jan 31 2026", "1/31/202",
                             "31/1/2026", "2026/1/31/5"})
        CHECK_FALSE(parseUserDate(text));

    CHECK(formatUserDate(jan31) == "01/31/2026");
    CHECK(parseUserDate(formatUserDate(makeDate(2025, 7, 4))) == makeDate(2025, 7, 4));
}

// ---- Session ---------------------------------------------------------------

namespace {

std::vector<Account> twoAccounts()
{
    return {testing::chaseJoint(), testing::fidelitySingle()};
}

}  // namespace

TEST_CASE("A session lists the folder's PDFs as blank rows")
{
    testing::TempDir dir;
    dir.touch("b.pdf");
    dir.touch("a.pdf");
    dir.touch("notes.txt");

    RenameSession s(dir.path(), twoAccounts());
    REQUIRE(s.size() == 2);
    CHECK(s.row(0).path.filename() == "a.pdf");
    CHECK(s.row(0).isBlank());
    CHECK_FALSE(s.hasUnappliedEntries());

    const auto preview = s.preview({});
    CHECK(preview[0].status == MoveStatus::Incomplete);
}

TEST_CASE("Carry-forward fills only blank rows, with the next date")
{
    testing::TempDir dir;
    for (const char* f : {"1.pdf", "2.pdf", "3.pdf"}) dir.touch(f);
    RenameSession s(dir.path(), twoAccounts());

    s.row(0).accountId = 1;
    s.row(0).date = SingleDate{makeDate(2026, 1, 31)};
    CHECK(s.carryForward(0, 1));
    CHECK(s.row(1).accountId == 1);
    CHECK(std::get<SingleDate>(*s.row(1).date).date == makeDate(2026, 2, 28));

    // A row that already has something entered is left alone.
    s.row(2).accountId = 2;
    CHECK_FALSE(s.carryForward(1, 2));
    CHECK(s.row(2).accountId == 2);
    CHECK_FALSE(s.row(2).date);

    // Skipped rows don't pass anything on.
    s.row(2).accountId.reset();
    s.row(1).skip = true;
    CHECK_FALSE(s.carryForward(1, 2));
}

TEST_CASE("Partial apply renames finished rows and leaves the rest editable")
{
    testing::TempDir dir;
    const fs::path a = dir.touch("scan1.pdf", "A");
    dir.touch("scan2.pdf", "B");
    dir.touch("scan3.pdf", "C");
    RenameSession s(dir.path(), twoAccounts());

    s.row(0).accountId = 2;
    s.row(0).date = Quarter{2026, 1};
    s.row(1).skip = true;
    CHECK(s.hasUnappliedEntries());

    PlanOptions opts;
    opts.sort.byYear = true;
    const auto result = s.apply(opts);
    CHECK(result.failures.empty());
    REQUIRE(result.record.moves.size() == 1);

    CHECK(s.row(0).done);
    CHECK(s.row(0).path == s.root() / "2026" / "2026.Q1 Fidelity Brokerage 5678 (Jane Smith).pdf");
    CHECK(s.row(0).originalPath.filename() == "scan1.pdf");
    CHECK(testing::readFile(s.row(0).path) == "A");
    CHECK_FALSE(s.row(1).done);
    CHECK(fs::exists(dir.path() / "scan2.pdf"));
    CHECK_FALSE(s.hasUnappliedEntries());
    CHECK(s.nextPending(0) == 1u);

    // The preview reports finished rows as unchanged at their new location.
    const auto preview = s.preview(opts);
    CHECK(preview[0].status == MoveStatus::Unchanged);
    CHECK(preview[1].status == MoveStatus::Skipped);
    CHECK(preview[2].status == MoveStatus::Incomplete);

    // A second apply in the same session continues where the first stopped.
    s.row(2).accountId = 2;
    s.row(2).date = Quarter{2026, 2};
    CHECK(s.apply(opts).record.moves.size() == 1);
    CHECK(s.row(2).done);
}

TEST_CASE("Renamed files left in the folder aren't listed again on refresh")
{
    testing::TempDir dir;
    dir.touch("scan1.pdf");
    RenameSession s(dir.path(), twoAccounts());
    s.row(0).accountId = 1;
    s.row(0).date = SingleDate{makeDate(2026, 1, 31)};
    s.apply({});  // no subfolders: the renamed file stays in the folder
    REQUIRE(s.row(0).done);

    dir.touch("new arrival.pdf");
    s.refresh();
    REQUIRE(s.size() == 2);
    CHECK(s.row(0).done);
    CHECK(s.row(1).path.filename() == "new arrival.pdf");
}

TEST_CASE("Refresh keeps entries, adds new files at the end and drops vanished ones")
{
    testing::TempDir dir;
    dir.touch("b.pdf");
    const fs::path c = dir.touch("c.pdf");
    RenameSession s(dir.path(), twoAccounts());
    s.row(0).accountId = 1;

    dir.touch("a.pdf");   // sorts first, but is added at the end
    fs::remove(c);
    s.refresh();

    REQUIRE(s.size() == 2);
    CHECK(s.row(0).path.filename() == "b.pdf");
    CHECK(s.row(0).accountId == 1);
    CHECK(s.row(1).path.filename() == "a.pdf");
}

TEST_CASE("A file that can't be renamed keeps its entries and reports why")
{
    testing::TempDir dir;
    dir.touch("scan1.pdf");
    // A plain file squatting on the account folder's name: the folder can't be
    // created, so the move fails at Apply time (like a locked PDF would).
    dir.touch("Fidelity Brokerage 5678 (Jane Smith)", "not a folder");

    RenameSession s(dir.path(), twoAccounts());
    REQUIRE(s.size() == 1);
    s.row(0).accountId = 2;
    s.row(0).date = Quarter{2026, 1};

    PlanOptions opts;
    opts.sort.byAccount = true;
    REQUIRE(s.preview(opts)[0].status == MoveStatus::Ready);

    const auto result = s.apply(opts);
    CHECK(result.record.moves.empty());
    CHECK(result.failures.size() == 1);
    CHECK_FALSE(s.row(0).done);
    CHECK_FALSE(s.row(0).failure.empty());
    CHECK(s.row(0).accountId == 2);
    CHECK(fs::exists(dir.path() / "scan1.pdf"));
    CHECK_FALSE(s.canUndo());
}

TEST_CASE("undoLast restores files and makes their rows editable again")
{
    testing::TempDir dir;
    const fs::path a = dir.touch("scan1.pdf", "A");
    dir.touch("scan2.pdf", "B");
    RenameSession s(dir.path(), twoAccounts());

    PlanOptions opts;
    opts.sort.byAccount = true;
    s.row(0).accountId = 1;
    s.row(0).date = SingleDate{makeDate(2026, 1, 31)};
    s.apply(opts);
    s.row(1).accountId = 2;
    s.row(1).date = Quarter{2026, 1};
    s.apply(opts);
    REQUIRE(s.row(1).done);

    auto undone = s.undoLast();  // only the second batch
    REQUIRE(undone);
    CHECK(undone->result.restored == 1);
    CHECK_FALSE(s.row(1).done);
    CHECK(s.row(1).path.filename() == "scan2.pdf");
    CHECK(s.row(1).accountId == 2);  // entries kept
    CHECK(s.row(0).done);
    CHECK_FALSE(fs::exists(dir.path() / "Fidelity Brokerage 5678 (Jane Smith)"));

    REQUIRE(s.undoLast());
    CHECK(testing::readFile(a) == "A");
    CHECK_FALSE(s.row(0).done);
    CHECK_FALSE(s.undoLast());
}

TEST_CASE("A file can use an old number; the folder still shows all numbers")
{
    testing::TempDir dir;
    dir.touch("old.pdf");
    dir.touch("new.pdf");
    Account card = testing::fidelitySingle();
    card.lastFour = "9012";
    card.previousLastFour = {"5678"};
    RenameSession s(dir.path(), {card});

    s.row(0).accountId = card.id;   // new.pdf
    s.row(0).date = SingleDate{makeDate(2026, 1, 31)};
    s.row(1).accountId = card.id;   // old.pdf
    s.row(1).number = "5678";
    s.row(1).date = SingleDate{makeDate(2023, 1, 31)};

    PlanOptions opts;
    opts.sort.byAccount = true;
    const auto preview = s.preview(opts);
    const fs::path folder = s.root() / "Fidelity Brokerage 9012 (was x5678) (Jane Smith)";
    CHECK(preview[0].destination == folder / "2026.01.31 Fidelity Brokerage 9012 (Jane Smith).pdf");
    CHECK(preview[1].destination == folder / "2023.01.31 Fidelity Brokerage 5678 (Jane Smith).pdf");

    // Carry-forward keeps the chosen number.
    dir.touch("z later.pdf");
    s.refresh();
    REQUIRE(s.carryForward(1, 2));
    CHECK(s.row(2).number == "5678");
}

#include <catch2/catch_test_macros.hpp>

#include "finrenamer/DateSpec.h"

using namespace finrenamer;

TEST_CASE("Single dates are zero-padded")
{
    CHECK(formatDateSpec(SingleDate{makeDate(2026, 1, 5)}) == "2026.01.05");
    CHECK(formatDateSpec(SingleDate{makeDate(2026, 12, 31)}) == "2026.12.31");
}

TEST_CASE("Periods show start and end")
{
    const DateSpec p = Period{makeDate(2026, 1, 1), makeDate(2026, 1, 31)};
    CHECK(formatDateSpec(p) == "2026.01.01 - 2026.01.31");
}

TEST_CASE("Quarters use the 2026.Q1 format")
{
    CHECK(formatDateSpec(Quarter{2026, 1}) == "2026.Q1");
    CHECK(formatDateSpec(Quarter{2025, 4}) == "2025.Q4");
}

TEST_CASE("Filing year")
{
    CHECK(filingYear(SingleDate{makeDate(2026, 3, 15)}) == 2026);
    CHECK(filingYear(Quarter{2025, 3}) == 2025);

    SECTION("a period spanning New Year files under its end year")
    {
        CHECK(filingYear(Period{makeDate(2025, 12, 15), makeDate(2026, 1, 14)}) == 2026);
    }
}

TEST_CASE("Date validation")
{
    CHECK_FALSE(validateDateSpec(SingleDate{makeDate(2026, 2, 28)}));
    CHECK(validateDateSpec(SingleDate{makeDate(2026, 2, 30)}));   // no Feb 30
    CHECK(validateDateSpec(SingleDate{makeDate(2025, 2, 29)}));   // not a leap year
    CHECK_FALSE(validateDateSpec(SingleDate{makeDate(2024, 2, 29)}));

    CHECK(validateDateSpec(Period{makeDate(2026, 2, 1), makeDate(2026, 1, 1)}));  // backwards
    CHECK_FALSE(validateDateSpec(Period{makeDate(2026, 1, 1), makeDate(2026, 1, 1)}));

    CHECK(validateDateSpec(Quarter{2026, 0}));
    CHECK(validateDateSpec(Quarter{2026, 5}));
    CHECK_FALSE(validateDateSpec(Quarter{2026, 4}));
}

TEST_CASE("A whole year is written as just the year")
{
    CHECK(formatDateSpec(WholeYear{2026}) == "2026");
    CHECK(filingYear(WholeYear{2025}) == 2025);
    CHECK_FALSE(validateDateSpec(WholeYear{2026}));
    CHECK(validateDateSpec(WholeYear{1800}));
    CHECK(validateDateSpec(WholeYear{10000}));

    SECTION("it reads back")
    {
        const auto parsed = parseDateSpec("2026");
        REQUIRE(parsed);
        const auto* year = std::get_if<WholeYear>(&*parsed);
        REQUIRE(year);
        CHECK(year->year == 2026);
        CHECK_FALSE(parseDateSpec("202"));
        CHECK_FALSE(parseDateSpec("20x6"));
        CHECK_FALSE(parseDateSpec("1800"));
    }
    SECTION("carry-forward moves to the next year")
    {
        const DateSpec following = nextDateSpec(WholeYear{2026});
        const auto* next = std::get_if<WholeYear>(&following);
        REQUIRE(next);
        CHECK(next->year == 2027);
    }
}

TEST_CASE("A year and month (no day) is written 2026.01")
{
    CHECK(formatDateSpec(YearMonth{2026, 1}) == "2026.01");
    CHECK(formatDateSpec(YearMonth{2026, 12}) == "2026.12");
    CHECK(filingYear(YearMonth{2025, 6}) == 2025);
    CHECK_FALSE(validateDateSpec(YearMonth{2026, 12}));
    CHECK(validateDateSpec(YearMonth{2026, 0}));
    CHECK(validateDateSpec(YearMonth{2026, 13}));

    SECTION("it reads back, and is not mistaken for a quarter")
    {
        const auto parsed = parseDateSpec("2026.03");
        REQUIRE(parsed);
        const auto* ym = std::get_if<YearMonth>(&*parsed);
        REQUIRE(ym);
        CHECK(ym->year == 2026);
        CHECK(ym->month == 3);
        CHECK(std::holds_alternative<Quarter>(*parseDateSpec("2026.Q3")));
        CHECK_FALSE(parseDateSpec("2026.13"));
        CHECK_FALSE(parseDateSpec("2026.00"));
        CHECK_FALSE(parseDateSpec("2026-03"));
    }
    SECTION("carry-forward moves to the next month and rolls over the year")
    {
        const DateSpec a = nextDateSpec(YearMonth{2026, 1});
        CHECK(std::get<YearMonth>(a).month == 2);
        const DateSpec b = nextDateSpec(YearMonth{2026, 12});
        CHECK(std::get<YearMonth>(b).year == 2027);
        CHECK(std::get<YearMonth>(b).month == 1);
    }
}

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

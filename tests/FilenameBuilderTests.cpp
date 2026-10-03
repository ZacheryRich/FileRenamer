#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "finrenamer/FilenameBuilder.h"

using namespace finrenamer;

TEST_CASE("Full filename in the required format")
{
    CHECK(buildFilename(testing::chaseJoint(), SingleDate{makeDate(2026, 1, 31)}) ==
          "2026.01.31 Chase Checking 1234 (John Smith; Jane Smith).pdf");

    CHECK(buildFilename(testing::fidelitySingle(), Quarter{2026, 1}) ==
          "2026.Q1 Fidelity Brokerage 5678 (Jane Smith).pdf");

    CHECK(buildFilename(testing::fidelitySingle(),
                        Period{makeDate(2026, 1, 1), makeDate(2026, 3, 31)}) ==
          "2026.01.01 - 2026.03.31 Fidelity Brokerage 5678 (Jane Smith).pdf");
}

TEST_CASE("Owners are joined with semicolons in display order")
{
    Account a = testing::chaseJoint();
    a.owners = {"A", "B", "C"};
    CHECK(accountLabel(a) == "Chase Checking 1234 (A; B; C)");

    a.owners = {"C", "A"};
    CHECK(accountLabel(a) == "Chase Checking 1234 (C; A)");

    a.owners.clear();
    CHECK(accountLabel(a) == "Chase Checking 1234");
}

TEST_CASE("Free-text account types are kept as typed")
{
    Account a = testing::fidelitySingle();
    a.accountType = "Roth IRA";
    CHECK(accountLabel(a) == "Fidelity Roth IRA 5678 (Jane Smith)");
}

TEST_CASE("Characters Windows forbids are cleaned up")
{
    CHECK(sanitizeComponent("Wells Fargo / Wachovia") == "Wells Fargo - Wachovia");
    CHECK(sanitizeComponent("A:B|C\\D") == "A-B-C-D");
    CHECK(sanitizeComponent("What? <Bank>* \"Inc\"") == "What Bank Inc");
    CHECK(sanitizeComponent("  Extra   spaces\tand\nnewlines  ") == "Extra spaces and newlines");
    CHECK(sanitizeComponent("Smith Jr.") == "Smith Jr");  // trailing dot not allowed
    CHECK(sanitizeComponent("José Núñez") == "José Núñez");  // non-ASCII untouched
}

TEST_CASE("Account validation")
{
    CHECK_FALSE(validateAccount(testing::chaseJoint()));

    Account a = testing::chaseJoint();
    a.institution = "  ";
    CHECK(validateAccount(a));

    a = testing::chaseJoint();
    a.lastFour = "";
    CHECK(validateAccount(a));

    a = testing::chaseJoint();
    a.owners.clear();
    CHECK_FALSE(validateAccount(a));  // owners are optional
}

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "finrenamer/FilenameBuilder.h"

using namespace finrenamer;

TEST_CASE("Full filename in the required format")
{
    CHECK(buildFilename(testing::chaseJoint(), SingleDate{makeDate(2026, 1, 31)}) ==
          "2026.01.31 Chase Checking x1234 (John Smith; Jane Smith).pdf");

    CHECK(buildFilename(testing::fidelitySingle(), Quarter{2026, 1}) ==
          "2026.Q1 Fidelity Brokerage x5678 (Jane Smith).pdf");

    CHECK(buildFilename(testing::fidelitySingle(),
                        Period{makeDate(2026, 1, 1), makeDate(2026, 3, 31)}) ==
          "2026.01.01 - 2026.03.31 Fidelity Brokerage x5678 (Jane Smith).pdf");
}

TEST_CASE("Owners are joined with semicolons in display order")
{
    Account a = testing::chaseJoint();
    a.owners = {"A", "B", "C"};
    CHECK(accountLabel(a) == "Chase Checking x1234 (A; B; C)");

    a.owners = {"C", "A"};
    CHECK(accountLabel(a) == "Chase Checking x1234 (C; A)");

    a.owners.clear();
    CHECK(accountLabel(a) == "Chase Checking x1234");
}

TEST_CASE("Free-text account types are kept as typed")
{
    Account a = testing::fidelitySingle();
    a.accountType = "Roth IRA";
    CHECK(accountLabel(a) == "Fidelity Roth IRA x5678 (Jane Smith)");
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

TEST_CASE("Previous numbers: folder shows them, file names don't")
{
    Account a = testing::chaseJoint();
    a.accountType = "Credit Card";
    a.lastFour = "9012";
    a.previousLastFour = {"5678", "1234"};  // newest first
    a.owners = {"H"};

    CHECK(accountLabel(a) == "Chase Credit Card x9012 (H)");
    CHECK(accountLabel(a, "1234") == "Chase Credit Card x1234 (H)");
    CHECK(accountFolderLabel(a) == "Chase Credit Card x9012 (was x5678, x1234) (H)");
    CHECK(buildFilename(a, Quarter{2024, 2}, ".pdf", "5678") ==
          "2024.Q2 Chase Credit Card x5678 (H).pdf");

    a.previousLastFour = {"1234"};
    CHECK(accountFolderLabel(a) == "Chase Credit Card x9012 (was x1234) (H)");

    a.previousLastFour.clear();
    CHECK(accountFolderLabel(a) == accountLabel(a));  // unchanged when there are none
}

TEST_CASE("Combined statements list each account's type and number")
{
    Account c;
    c.institution = "JPMorgan Chase";
    c.institutionDisplay = "Chase";
    c.combined = {{1, "Chk", "1111"}, {2, "Sav", "2222"}, {3, "Chk", "3333"}};
    c.owners = {"H"};
    CHECK(accountLabel(c) == "Chase Chk x1111, Sav x2222, Chk x3333 (H)");
    CHECK(accountFolderLabel(c) == accountLabel(c));  // the folder is named the same way
    CHECK(buildFilename(c, SingleDate{makeDate(2026, 1, 31)}) ==
          "2026.01.31 Chase Chk x1111, Sav x2222, Chk x3333 (H).pdf");
    CHECK(accountLabel(c, "9999") == accountLabel(c));  // per-file numbers don't apply

    c.owners = {"H", "W"};
    c.combined = {{1, "Checking", "1111"}, {2, "Savings", "2222"}};
    CHECK(accountLabel(c) == "Chase Checking x1111, Savings x2222 (H; W)");
    CHECK_FALSE(validateAccount(c));

    c.combined.pop_back();
    CHECK(validateAccount(c));  // needs two accounts
    c.combined = {{1, "Checking", "1111"}, {2, "", "2222"}};
    CHECK_FALSE(validateAccount(c));  // a blank type is fine now
    c.combined = {{1, "Checking", "1111"}, {2, "Savings", ""}};
    CHECK(validateAccount(c));        // a blank number is not
}

TEST_CASE("The account type may be left blank")
{
    Account a;
    a.institution = "Chase";
    a.lastFour = "1234";
    a.owners = {"H"};
    CHECK_FALSE(validateAccount(a));
    CHECK(accountLabel(a) == "Chase x1234 (H)");
    CHECK(buildFilename(a, SingleDate{makeDate(2026, 1, 31)}) == "2026.01.31 Chase x1234 (H).pdf");
    a.accountType = "   ";
    CHECK_FALSE(validateAccount(a));
    CHECK(accountLabel(a) == "Chase x1234 (H)");

    a.lastFour = "";
    CHECK(validateAccount(a).has_value());  // the number is still needed

    // On a combined statement a blank type leaves just the number: "Chase x1111, Sav x2222".
    Account combined;
    combined.institution = "Chase";
    combined.combined = {{1, "", "1111"}, {2, "Sav", "2222"}};
    CHECK_FALSE(validateAccount(combined));
    CHECK(accountLabel(combined) == "Chase x1111, Sav x2222");
}

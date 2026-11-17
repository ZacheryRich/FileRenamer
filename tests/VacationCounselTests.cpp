#include <catch2/catch_test_macros.hpp>

#include "finrenamer/VacationCounsel.h"

using namespace finrenamer;

namespace {

struct Directory {
    std::vector<Firm> firms{
        {1, "Alpha Law", "555-1000", "", "100 First St\nSuite 2\nAustin, TX 78701", false},
        {2, "Beta & Co", "", "", "200 Second Ave\nDallas, TX 75201", false},
        {3, "Gamma PC", "", "", "", false},
    };
    std::vector<Attorney> attorneys{
        {10, 1, "Ann Able", "1", "ann@alpha.example"},
        {11, 1, "Carl Cole", "2", ""},
        {20, 2, "Bea Brown", "3", "bea@beta.example"},
        {21, 2, "Ann Able", "4", "ANN@alpha.example"},  // same name at another firm, same email
        {30, 3, "Gus Grey", "", "gus@gamma.example"},
    };
};

}  // namespace

TEST_CASE("Opposing counsel are written into the sheet's columns")
{
    Directory d;
    const auto text = opposingCounselText({
        {d.firms[0], {d.attorneys[0], d.attorneys[1]}},
        {d.firms[1], {d.attorneys[2], d.attorneys[3]}},
        {d.firms[2], {d.attorneys[4]}},
    });
    CHECK(text.names == "Ann Able\nCarl Cole");
    CHECK(text.firmName == "Alpha Law");
    CHECK(text.address == "100 First St\nSuite 2\nAustin, TX 78701");
    CHECK(text.others == "Bea Brown\nAnn Able\nBeta & Co\n200 Second Ave\nDallas, TX 75201\n\nGus Grey\nGamma PC");
    CHECK(text.emails == "ann@alpha.example; bea@beta.example; gus@gamma.example");  // repeat dropped
}

TEST_CASE("One firm, no attorneys, or nothing at all")
{
    Directory d;
    const auto one = opposingCounselText({{d.firms[1], {}}});
    CHECK(one.names.empty());
    CHECK(one.firmName == "Beta & Co");
    CHECK(one.others.empty());
    CHECK(one.emails.empty());
    const auto none = opposingCounselText({});
    CHECK(none.firmName.empty());
    CHECK(none.address.empty());
}

TEST_CASE("A case's opposing counsel text is read back into choices")
{
    Directory d;
    const auto text = opposingCounselText({
        {d.firms[0], {d.attorneys[1]}},
        {d.firms[1], {d.attorneys[3], d.attorneys[2]}},
        {d.firms[2], {}},
    });
    const auto m = matchOpposingCounsel(text.names, text.firmName, text.others, d.firms, d.attorneys);
    CHECK(m.unmatched.empty());
    REQUIRE(m.choices.size() == 3);
    CHECK(m.choices[0].firm.id == 1);
    REQUIRE(m.choices[0].attorneys.size() == 1);
    CHECK(m.choices[0].attorneys[0].id == 11);
    CHECK(m.choices[1].firm.id == 2);
    REQUIRE(m.choices[1].attorneys.size() == 2);
    CHECK(m.choices[1].attorneys[0].id == 21);  // the Ann Able at Beta, not the one at Alpha
    CHECK(m.choices[1].attorneys[1].id == 20);
    CHECK(m.choices[2].firm.id == 3);
    CHECK(m.choices[2].attorneys.empty());
}

TEST_CASE("Text typed by hand is matched exactly or reported, never guessed")
{
    Directory d;
    const auto m = matchOpposingCounsel("  ann able \nSomeone New", "ALPHA LAW", "Bea Brown\nDelta LLP\n1 Road\n\n\nBea Brown\nbeta & co",
                                        d.firms, d.attorneys);
    REQUIRE(m.choices.size() == 2);
    CHECK(m.choices[0].firm.id == 1);
    REQUIRE(m.choices[0].attorneys.size() == 1);
    CHECK(m.choices[0].attorneys[0].id == 10);
    CHECK(m.choices[1].firm.id == 2);
    CHECK(m.choices[1].attorneys.size() == 1);
    REQUIRE(m.unmatched.size() == 2);
    CHECK(m.unmatched[0] == "OPC: Someone New");
    CHECK(m.unmatched[1] == "OPC 2 Address: Bea Brown ...");  // the Delta LLP block: no such firm

    const auto unknownFirm = matchOpposingCounsel("Ann Able", "Nobody & Co", "", d.firms, d.attorneys);
    CHECK(unknownFirm.choices.empty());
    CHECK(unknownFirm.unmatched.size() == 2);
    CHECK(matchOpposingCounsel("", "", "", d.firms, d.attorneys).choices.empty());
}


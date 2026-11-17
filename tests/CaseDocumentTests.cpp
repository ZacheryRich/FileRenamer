#include <catch2/catch_test_macros.hpp>

#include "finrenamer/CaseDocument.h"
#include "finrenamer/DateSpec.h"

using namespace finrenamer;

namespace {

const auto kToday = makeDate(2026, 11, 10);

CaseFile sampleCase()
{
    CaseFile f;
    f.info.clientName = "Smith";
    f.info.causeNumber = "D-1-FM-26-000123";
    f.info.county = "Travis";
    f.info.court = "345th Judicial District";
    f.info.caseType = "Divorce";
    CaseParty pat;
    pat.id = 1;
    pat.name = "Pat Smith";
    pat.role = PartyRole::Petitioner;
    CaseParty robin;
    robin.id = 2;
    robin.name = "Robin Smith";
    robin.role = PartyRole::Respondent;
    robin.ourClient = true;
    robin.address = "1 Main St";
    robin.cityStateZip = "Austin, TX 78701";
    robin.driversLicense = "12345678";
    f.parties = {pat, robin};
    f.children = {{1, 0, "Kim Smith", "", "F", "3/4/2015", "Austin", "Elm", ""}, {2, 0, "Lee Smith", "9", "M"}};
    f.contacts = {{1, 0, "Prior Counsel", "Old Lawyer", "Old Firm", "111", "9 Elm St\nDallas, TX 75201", "", "", "old@x.test"}};
    const Firm ours{1, "Our Firm", "512-555-0100", "512-555-0101", "6 Court Dr\nSuite 1\nAustin, Texas 78730", true};
    const Firm opp{2, "Opp Law", "512-555-0200", "512-555-0201", "2 Elm St\nRound Rock, TX 78664", false};
    f.attorneys = {{{10, 1, "Michael Burnett", "1", "mb@ours.test", ""}, ours},
                   {{11, 1, "Scott A. Nyitray", "2", "san@ours.test", "Very truly yours,"}, ours},
                   {{20, 2, "Oscar Opp", "3", "oscar@opp.test"}, opp}};
    return f;
}

std::string get(const MergeRecord& r, const char* key)
{
    const auto it = r.find(key);
    return it == r.end() ? std::string("<none>") : it->second;
}

}  // namespace

TEST_CASE("Helpers for case documents")
{
    CHECK(initialsOf("Scott A. Nyitray") == "SAN");
    CHECK(initialsOf("Travis L. Turner") == "TLT");
    CHECK(initialsOf("") == "");
    CHECK(joinNames({}) == "");
    CHECK(joinNames({"A", " "}) == "A");
    CHECK(joinNames({"A", "B"}) == "A and B");
    CHECK(joinNames({"A", "B", "C"}) == "A, B, and C");
    CHECK(ageOn("3/4/2015", kToday) == "11");
    CHECK(ageOn("11/10/2015", kToday) == "11");
    CHECK(ageOn("11/11/2015", kToday) == "10");
    CHECK(ageOn("not a date", kToday) == "");
    CHECK(ageOn("1/1/2030", kToday) == "");
    CHECK(longDate(kToday) == "November 10, 2026");
    CHECK(addressStreet("6 Court Dr\nSuite 1\nAustin, Texas 78730") == "6 Court Dr\nSuite 1");
    CHECK(addressCityLine("6 Court Dr\nSuite 1\nAustin, Texas 78730") == "Austin, Texas 78730");
    CHECK(addressCityLine("one line only") == "");
    CHECK(addressStreet("one line only") == "one line only");
}

TEST_CASE("A case becomes fields and groups for a template")
{
    const auto d = caseMergeData(sampleCase(), 11, kToday);
    const auto& f = d.fields;
    CHECK(get(f, "causenumber") == "D-1-FM-26-000123");
    CHECK(get(f, "court") == "345th Judicial District");
    CHECK(get(f, "petitioner") == "Pat Smith");
    CHECK(get(f, "respondent") == "Robin Smith");
    CHECK(get(f, "client") == "Robin Smith");
    CHECK(get(f, "clientrole") == "Respondent");
    CHECK(get(f, "clientcitystatezip") == "Austin, TX 78701");
    CHECK(get(f, "clienttxdl") == "12345678");
    CHECK(get(f, "opposingparty") == "Pat Smith");
    CHECK(get(f, "opposingpartyrole") == "Petitioner");
    CHECK(get(f, "childrennames") == "Kim Smith and Lee Smith");
    CHECK(get(f, "childorchildren") == "CHILDREN");
    // the chosen signer
    CHECK(get(f, "ourattorney") == "Scott A. Nyitray");
    CHECK(get(f, "ourbarnumber") == "2");
    CHECK(get(f, "oursbn") == "2");
    CHECK(get(f, "signoff") == "Very truly yours,");
    CHECK(get(f, "initials") == "SAN");
    CHECK(get(f, "ourcitystatezip") == "Austin, Texas 78730");
    CHECK(get(f, "opc") == "Oscar Opp");
    CHECK(get(f, "opcfirm") == "Opp Law");
    CHECK(get(f, "opcaddress") == "2 Elm St\nRound Rock, TX 78664");
    CHECK(get(f, "opcstreet") == "2 Elm St");
    CHECK(get(f, "opcfax") == "512-555-0201");
    CHECK(get(f, "today") == "November 10, 2026");

    REQUIRE(d.groups.at("our").size() == 2);
    CHECK(get(d.groups.at("our")[0], "ourattorney") == "Scott A. Nyitray");  // signer first
    CHECK(get(d.groups.at("our")[1], "oursignoff") == "Sincerely,");
    REQUIRE(d.groups.at("children").size() == 2);
    CHECK(get(d.groups.at("children")[0], "childage") == "11");  // from the date of birth
    CHECK(get(d.groups.at("children")[1], "childage") == "9");   // as typed
    REQUIRE(d.groups.at("players").size() == 2);
    CHECK(get(d.groups.at("players")[0], "playerrole") == "Opposing Counsel");
    CHECK(get(d.groups.at("players")[1], "playerrole") == "Prior Counsel");
    CHECK(get(d.groups.at("players")[1], "playercitystatezip") == "Dallas, TX 75201");
    CHECK(d.groups.at("ifchildren").size() == 1);
    CHECK(d.groups.at("ifnochildren").empty());
    CHECK(d.groups.at("ifopc").size() == 1);
    CHECK(d.groups.at("client").size() == 1);
    CHECK(d.groups.at("parties").size() == 2);
}

TEST_CASE("A case with almost nothing filled in still gives every group")
{
    CaseFile f;
    const auto d = caseMergeData(f, 0, kToday);
    CHECK(get(d.fields, "client") == "");
    CHECK(get(d.fields, "signoff") == "Sincerely,");
    CHECK(get(d.fields, "childorchildren") == "");
    for (const char* g : {"our", "opc", "other", "players", "children", "client", "opposingparty", "parties"}) {
        INFO(g);
        CHECK(d.groups.count(g) == 1);
        CHECK(d.groups.at(g).empty());
    }
    CHECK(d.groups.at("ifnochildren").size() == 1);
    CHECK(d.groups.at("ifnoopc").size() == 1);

    // No client ticked: nobody counts as the other side.
    f.parties = {{1, 0, "Pat", PartyRole::Petitioner}};
    CHECK(get(caseMergeData(f, 0, kToday).fields, "opposingparty") == "");
}

TEST_CASE("Every field in the help list is filled for a full case")
{
    const auto d = caseMergeData(sampleCase(), 0, kToday);
    for (const FieldHelp& h : caseFieldReference()) {
        if (h.section == "How to use" || h.name.find("Begin_") != std::string::npos || h.name == "Signature") continue;
        // "A, B, C" entries list several fields
        std::string names = h.name;
        std::size_t start = 0;
        while (start < names.size()) {
            std::size_t comma = names.find(',', start);
            std::string one = names.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            while (!one.empty() && one.front() == ' ') one.erase(0, 1);
            std::string key;
            for (char c : one)
                if (std::isalnum(static_cast<unsigned char>(c))) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            INFO(one);
            CHECK(d.fields.count(key) == 1);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
}

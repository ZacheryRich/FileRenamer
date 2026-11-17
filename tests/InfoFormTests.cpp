#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include "TestHelpers.h"
#include "finrenamer/InfoForm.h"
#include "finrenamer/Utf8Path.h"

using namespace finrenamer;
using testing::TempDir;
namespace fs = std::filesystem;

// Everything here is made up.
namespace {

// A one-page PDF with fillable fields: text fields (name -> value) and radio groups (name -> state).
fs::path makeForm(const TempDir& dir, const std::vector<std::pair<std::string, std::string>>& text,
                  const std::vector<std::pair<std::string, std::string>>& radios)
{
    QPDF pdf;
    pdf.emptyPDF();
    auto page = pdf.makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Page /MediaBox [0 0 612 792] >>"));
    pdf.addPage(page, false);
    auto fields = QPDFObjectHandle::newArray();
    auto annots = QPDFObjectHandle::newArray();
    int y = 700;
    for (const auto& [name, value] : text) {
        auto f = pdf.makeIndirectObject(QPDFObjectHandle::parse(
            "<< /FT /Tx /Type /Annot /Subtype /Widget /Rect [72 " + std::to_string(y) + " 300 " + std::to_string(y + 14) + "] >>"));
        f.replaceKey("/T", QPDFObjectHandle::newUnicodeString(name));
        f.replaceKey("/V", QPDFObjectHandle::newUnicodeString(value));
        fields.appendItem(f);
        annots.appendItem(f);
        y -= 16;
    }
    for (const auto& [name, state] : radios) {
        auto parent = pdf.makeIndirectObject(QPDFObjectHandle::parse("<< /FT /Btn /Ff 49152 >>"));
        parent.replaceKey("/T", QPDFObjectHandle::newUnicodeString(name));
        parent.replaceKey("/V", QPDFObjectHandle::newName(state));
        auto kids = QPDFObjectHandle::newArray();
        for (const char* choice : {"/Choice1", "/Choice2"}) {
            auto kid = pdf.makeIndirectObject(QPDFObjectHandle::parse(
                "<< /Type /Annot /Subtype /Widget /Rect [72 100 90 118] /AP << /N << " + std::string(choice) +
                " null /Off null >> >> >>"));
            kid.replaceKey("/Parent", parent);
            kid.replaceKey("/AS", QPDFObjectHandle::newName(std::string(choice) == state ? choice : "/Off"));
            kids.appendItem(kid);
            annots.appendItem(kid);
        }
        parent.replaceKey("/Kids", kids);
        fields.appendItem(parent);
    }
    page.replaceKey("/Annots", annots);
    auto acro = QPDFObjectHandle::newDictionary();
    acro.replaceKey("/Fields", fields);
    pdf.getRoot().replaceKey("/AcroForm", pdf.makeIndirectObject(acro));
    const fs::path path = dir.path() / "form.pdf";
    QPDFWriter w(pdf, utf8FromPath(path).c_str());
    w.write();
    return path;
}

std::map<std::string, std::string> sampleFields()
{
    return {{"YOUR FULL NAME", "Casey Example"},
            {"MAIDEN NAME", "Former"},
            {"Date of Birth", "2/3/1985"},
            {"Place of Birth", "Dallas, Texas"},
            {"Social Security", "000-00-0000"},
            {"Drivers License", "12345678"},
            {"State", "TX"},
            {"Street Address", "22 Test Street"},
            {"CityStateZip", "Austin, TX 78701"},
            {"County of Residence", "Travis"},
            {"Group2", "/Choice2"},
            {"Street Address_2", "PO Box 1"},
            {"CityStateZip_2", "Austin, TX 78702"},
            {"Phone numbers Cell", "512-555-0120"},
            {"Home", "512-555-0121"},
            {"Work", ""},
            {"Email", "casey@example.test"},
            {"Your vehicle Year", "2019"},
            {"Make", "Example"},
            {"Model", "Sedan"},
            {"Employer", "Example Corp"},
            {"Gross Salary", "5,000"},
            {"Group11", "/Choice1"},
            {"Full Name", "Jordan Example"},
            {"Date of Birth_2", "4/5/1984"},
            {"Phone Numbers Cell", "512-555-0130"},
            {"Street Address_4", "10 Sample Lane"},
            {"CityStateZip_4", "Round Rock, TX 78664"},
            {"Automobile Year", "2020"},
            {"Make_2", "Other"},
            {"Model_2", "Truck"},
            {"OPPOSING PARTYS ATTORNEY if known", "Morgan Opposing"},
            {"Date", "6/1/2010"},
            {"City State", "Austin, Texas"},
            {"If separated date of separation", "1/15/2026"},
            {"Name First Middle  LastRow1", "Riley Example"},
            {"Sex MFRow1", "F"},
            {"DOBRow1", "3/4/2015"},
            {"Place of Birth City County  StateRow1", "Austin, Travis, Texas"},
            {"Name First Middle LastRowe3", "Avery Example"},
            {"Sex MFRow3", "M"},
            {"children's property", "A savings account"},
            {"Person who carries insurance", "Jordan Example"},
            {"Name of Insurance Company", "Example Health"},
            {"Policy Number", "P-1"},
            {"Group Number", "G-2"},
            {"Group14a", "/Choice2"},
            {"Other", ""},
            {"First", "Casey"},
            {"Middle", "Q"},
            {"Last", "Former"}};
}

}  // namespace

TEST_CASE("A PDF's fillable fields are read, text and radio buttons")
{
    TempDir dir;
    const auto pdf = makeForm(dir, {{"YOUR FULL NAME", "Casey Exámple"}, {"Email", ""}}, {{"Group2", "/Choice2"}, {"Group11", "/Off"}});
    const auto fields = readPdfFormFields(pdf);
    CHECK(fields.at("YOUR FULL NAME") == "Casey Exámple");
    CHECK(fields.at("Email").empty());
    CHECK(fields.at("Group2") == "/Choice2");
    CHECK(fields.at("Group11").empty());  // not chosen
    CHECK_THROWS_WITH(readPdfFormFields(dir.path() / "gone.pdf"), Catch::Matchers::ContainsSubstring("not found"));
    CHECK_THROWS_WITH(readPdfFormFields(dir.touch("junk.pdf", "not a pdf")), Catch::Matchers::ContainsSubstring("Couldn't read"));
}

TEST_CASE("The Information Form's fields become case information")
{
    const auto d = parseInfoForm(sampleFields());
    CHECK(d.fieldsFound > 40);
    CHECK(d.client.name == "Casey Example");
    CHECK(d.client.ourClient);
    CHECK(d.client.driversLicense == "12345678 (TX)");
    CHECK(d.client.phone == "Cell: 512-555-0120; Home: 512-555-0121");
    CHECK(d.client.car == "2019 Example Sedan");
    CHECK(d.client.nameChange == "Casey Q Former");
    CHECK(d.opposing.name == "Jordan Example");
    CHECK(d.opposing.phone == "512-555-0130");  // one number: no label
    CHECK(d.opposing.cityStateZip == "Round Rock, TX 78664");
    CHECK(d.marriageDate == "6/1/2010");
    CHECK(d.marriagePlace == "Austin, Texas");
    CHECK(d.separationDate == "1/15/2026");
    CHECK(d.insurance == "Carried by Jordan Example");
    CHECK(d.groupNumber == "G-2");
    CHECK(d.opposingAttorney == "Morgan Opposing");
    REQUIRE(d.children.size() == 2);
    CHECK(d.children[0].pob == "Austin, Travis, Texas");
    CHECK(d.children[1].name == "Avery Example");  // the "Rowe3" row
    CHECK(d.children[1].sex == "M");
    auto has = [&](const std::string& label, const std::string& value) {
        for (const auto& [l, v] : d.extras)
            if (l == label) return v == value;
        return false;
    };
    CHECK(has("Client's maiden name", "Former"));
    CHECK(has("Is this the client's mailing address", "No"));
    CHECK(has("Client's mailing address", "PO Box 1, Austin, TX 78702"));
    CHECK(has("Client's gross salary", "5,000 (monthly)"));
    CHECK(has("Property owned by the children", "A savings account"));
    CHECK(has("Insurance premium paid", "Annual"));

    CHECK(parseInfoForm({{"Some other form", "x"}}).fieldsFound == 0);
}

TEST_CASE("Importing into a new case fills everything and ticks it all")
{
    const auto form = parseInfoForm(sampleFields());
    const std::vector<Attorney> directory{{7, 2, "Morgan Opposing"}, {8, 2, "Someone Else"}};
    CaseFile file;  // a new case
    auto changes = planFormImport(file, form, directory, "From the Information Form:");
    for (const auto& c : changes) {
        INFO(c.section << " / " << c.label);
        CHECK(c.apply);
    }
    const auto attorneys = applyFormChanges(file, form, changes, PartyRole::Respondent);
    CHECK(attorneys == std::vector<std::int64_t>{7});
    CHECK(file.info.clientName == "Casey Example");
    CHECK(file.info.marriagePlace == "Austin, Texas");
    REQUIRE(file.parties.size() == 2);
    CHECK(file.parties[0].ourClient);
    CHECK(file.parties[0].role == PartyRole::Respondent);
    CHECK(file.parties[0].ssn == "000-00-0000");
    CHECK(file.parties[1].name == "Jordan Example");
    CHECK(file.parties[1].role == PartyRole::Petitioner);
    CHECK_FALSE(file.parties[1].ourClient);
    REQUIRE(file.children.size() == 2);
    CHECK(file.children[0].dob == "3/4/2015");
    CHECK_THAT(file.info.notes, Catch::Matchers::StartsWith("From the Information Form:\n"));
    CHECK_THAT(file.info.notes, Catch::Matchers::ContainsSubstring("Client's maiden name: Former"));
}

TEST_CASE("Importing into an existing case never overwrites without asking")
{
    const auto form = parseInfoForm(sampleFields());
    CaseFile file;
    file.info.id = 5;
    file.info.clientName = "Example";
    file.info.marriageDate = "6/2/2010";  // differs from the form
    file.info.notes = "Earlier notes.";
    CaseParty client;
    client.id = 1;
    client.name = "Casey Example";
    client.role = PartyRole::Petitioner;
    client.ourClient = true;
    client.dob = "2/3/1985";      // same as the form: not listed
    client.phone = "512-555-9999";  // differs
    CaseParty other;
    other.id = 2;
    other.name = "J. Example";    // a different spelling: matched by role
    other.role = PartyRole::Respondent;
    file.parties = {client, other};
    file.children = {{3, 5, "riley example", "", "F"}};  // same child, different case of letters
    const std::vector<Attorney> directory{{7, 2, "Morgan Opposing"}, {9, 3, "Morgan Opposing"}};  // two of that name

    auto changes = planFormImport(file, form, directory, "From the form:");
    auto find = [&](const std::string& section, const std::string& label) -> FormChange* {
        for (auto& c : changes)
            if (c.section == section && c.label == label) return &c;
        return nullptr;
    };
    CHECK(find("Case", "Client name (Case List)") == nullptr);  // the case keeps its own name
    REQUIRE(find("Case", "Date of marriage"));
    CHECK_FALSE(find("Case", "Date of marriage")->apply);       // would replace something different
    CHECK(find("Case", "Date of marriage")->current == "6/2/2010");
    CHECK(find("Case", "Place of marriage")->apply);              // blank now
    CHECK(find("Our client", "Date of birth") == nullptr);       // already the same
    REQUIRE(find("Our client", "Phone"));
    CHECK_FALSE(find("Our client", "Phone")->apply);
    REQUIRE(find("Opposing party", "Name"));
    CHECK_FALSE(find("Opposing party", "Name")->apply);
    CHECK(find("Child: Riley Example", "Add this child") == nullptr);  // matched by name
    CHECK(find("Child: Riley Example", "Date of birth")->apply);
    CHECK(find("Child: Avery Example", "Add this child")->apply);
    CHECK(find("Attorneys", "Add to the case (from the Attorney Directory)") == nullptr);  // ambiguous: not guessed
    REQUIRE(find("Notes", "Added to the case's notes"));
    CHECK_THAT(find("Notes", "Added to the case's notes")->value,
               Catch::Matchers::ContainsSubstring("Opposing party's attorney (not in the Attorney Directory): Morgan Opposing"));

    find("Our client", "Phone")->apply = true;  // the user ticks it
    applyFormChanges(file, form, changes, PartyRole::Respondent);
    CHECK(file.parties.size() == 2);
    CHECK(file.parties[0].phone == "Cell: 512-555-0120; Home: 512-555-0121");
    CHECK(file.parties[0].role == PartyRole::Petitioner);  // an existing party keeps its role
    CHECK(file.parties[1].name == "J. Example");           // left alone (unticked)
    CHECK(file.parties[1].address == "10 Sample Lane");     // blank before: filled
    CHECK(file.info.marriageDate == "6/2/2010");
    CHECK(file.info.marriagePlace == "Austin, Texas");
    REQUIRE(file.children.size() == 2);
    CHECK(file.children[0].dob == "3/4/2015");
    CHECK(file.children[1].name == "Avery Example");
    CHECK_THAT(file.info.notes, Catch::Matchers::StartsWith("Earlier notes.\n\nFrom the form:"));
}

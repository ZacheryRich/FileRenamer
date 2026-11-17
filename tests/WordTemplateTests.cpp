#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <OpenXLSX.hpp>

#include "TestHelpers.h"
#include "finrenamer/Utf8Path.h"
#include "finrenamer/WordTemplate.h"

using namespace finrenamer;
using testing::TempDir;

namespace {

std::string fld(const std::string& code, const std::string& rpr = "<w:rPr><w:b/></w:rPr>")
{
    return "<w:r>" + rpr + "<w:fldChar w:fldCharType=\"begin\"/></w:r><w:r>" + rpr +
           "<w:instrText xml:space=\"preserve\"> " + code + " </w:instrText></w:r><w:r>" + rpr +
           "<w:fldChar w:fldCharType=\"separate\"/></w:r><w:r>" + rpr + "<w:t>«x»</w:t></w:r><w:r>" + rpr +
           "<w:fldChar w:fldCharType=\"end\"/></w:r>";
}
std::string mf(const std::string& name) { return fld("MERGEFIELD " + name); }
std::string txt(const std::string& t) { return "<w:r><w:t xml:space=\"preserve\">" + t + "</w:t></w:r>"; }
std::string para(const std::string& runs) { return "<w:p>" + runs + "</w:p>"; }
std::string cell(const std::string& paras) { return "<w:tc><w:tcPr/>" + paras + "</w:tc>"; }

std::filesystem::path makeDocx(const TempDir& dir, const std::string& body, const std::string& header = {},
                               const std::string& name = "t.docx")
{
    const std::string ns = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";
    std::vector<std::pair<std::string, std::string>> entries{
        {"[Content_Types].xml",
         "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
         "<Default Extension=\"xml\" ContentType=\"application/xml\"/></Types>"},
        {"word/document.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:document " + ns +
                                  "><w:body>" + body + "<w:sectPr><w:pgSz w:w=\"12240\"/></w:sectPr></w:body></w:document>"},
        {"word/_rels/document.xml.rels",
         "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
         "<Relationship Id=\"rId1\" Type=\"x\" Target=\"styles.xml\"/></Relationships>"}};
    if (!header.empty()) entries.push_back({"word/header1.xml", "<w:hdr " + ns + ">" + header + "</w:hdr>"});
    const auto file = dir.path() / name;
    testing::writeZip(file, entries);
    return file;
}

std::string entry(const std::filesystem::path& file, const std::string& name)
{
    OpenXLSX::XLZipArchive zip;
    zip.open(utf8FromPath(file));
    const std::string s = zip.getEntry(name);
    zip.close();
    return s;
}

// The text of a part: one line per paragraph, "|" between table cells, "/" for a line break.
std::string textOf(const std::string& xml)
{
    std::string out;
    std::size_t i = 0;
    while (i < xml.size()) {
        if (xml.compare(i, 5, "<w:t>") == 0 || xml.compare(i, 5, "<w:t ") == 0) {
            const std::size_t open = xml.find('>', i);
            const std::size_t close = xml.find("</w:t>", open);
            out += xml.substr(open + 1, close - open - 1);
            i = close + 6;
        } else if (xml.compare(i, 6, "<w:br/") == 0) {
            out += "/";
            i += 6;
        } else if (xml.compare(i, 6, "</w:p>") == 0) {
            out += "\n";
            i += 6;
        } else if (xml.compare(i, 5, "<w:p ") == 0 || xml.compare(i, 5, "<w:p/") == 0) {
            const std::size_t gt = xml.find('>', i);
            if (xml[gt - 1] == '/') out += "\n";  // an empty paragraph
            i = gt + 1;
        } else if (xml.compare(i, 7, "</w:tc>") == 0) {
            out += "|";
            i += 7;
        } else {
            ++i;
        }
    }
    return out;
}

MergeRecord rec(std::initializer_list<std::pair<const std::string, std::string>> v) { return MergeRecord(v); }

}  // namespace

TEST_CASE("Merge fields are filled, with Word's switches, and blank-only lines dropped")
{
    TempDir dir;
    const auto tmpl = makeDocx(
        dir, para(fld("DATE \\@ \"MMMM d, yyyy\"")) +
                 para(txt("Cause No. ") + mf("Cause_Number")) +
                 para(mf("County \\* Upper") + txt(" COUNTY, TEXAS")) +
                 para(mf("Fax \\b \"Fax: \"")) +           // blank: the whole line goes
                 para(txt("Tel: ") + mf("Phone")) +         // blank but has a label: stays
                 para(mf("Name \\* Caps \\f \", Esq.\"")) +
                 para(mf("Address")) + para(mf("Nobody_Knows")) +
                 para("<w:fldSimple w:instr=\" MERGEFIELD County \\* Lower \"><w:r><w:t>«County»</w:t></w:r></w:fldSimple>"));
    MergeData data;
    data.fields = rec({{"causenumber", "D-1-FM-26-000123"},
                       {"county", "Travis"},
                       {"fax", ""},
                       {"phone", ""},
                       {"name", "jane q. smith"},
                       {"address", "1 Main St\nAustin, TX 78701"}});
    const auto out = dir.path() / "out.docx";
    const auto r = fillWordTemplate(tmpl, out, data, "November 10, 2026");
    const std::string xml = entry(out, "word/document.xml");
    CHECK(textOf(xml) ==
          "November 10, 2026\nCause No. D-1-FM-26-000123\nTRAVIS COUNTY, TEXAS\nTel: \nJane Q. Smith, Esq.\n"
          "1 Main St/Austin, TX 78701\ntravis\n");
    CHECK(r.unknownFields == std::vector<std::string>{"Nobody_Knows"});
    CHECK(xml.find("<w:b/>") != std::string::npos);        // the field's formatting is kept
    CHECK(xml.find("MERGEFIELD") == std::string::npos);    // no fields left
    CHECK(xml.find("DATE \\@") != std::string::npos);      // the date field stays
    CHECK(xml.find("<w:sectPr>") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "out.docx.saving"));
    // the last paragraph held only an fldSimple whose value went lower case
    CHECK(xml.find(">travis<") != std::string::npos);
}

TEST_CASE("A block between Begin_ and End_ repeats once per item")
{
    TempDir dir;
    const auto tmpl = makeDocx(dir, para(txt("PLAYERS")) + para(mf("Begin_Players")) + para(txt("Role: ") + mf("Player_Role")) +
                                        para(mf("Player_Name") + txt(" for ") + mf("Client")) + para(mf("Player_Email")) +
                                        para(mf("End_Players")) + para(txt("Begin ") + mf("Begin_None") + txt("gone")) +
                                        para(txt("also gone") + mf("End_None")) + para(txt("END")));
    MergeData data;
    data.fields = rec({{"client", "Pat"}, {"playername", "top-level value is shadowed"}});
    data.groups["players"] = {rec({{"playerrole", "Opposing Counsel"}, {"playername", "Ann Able"}, {"playeremail", "ann@x.test"}}),
                              rec({{"playerrole", "Expert"}, {"playername", "Dr. Bo"}, {"playeremail", ""}})};
    data.groups["none"] = {};
    const auto out = dir.path() / "out.docx";
    const auto r = fillWordTemplate(tmpl, out, data, "today");
    CHECK(textOf(entry(out, "word/document.xml")) ==
          "PLAYERS\nRole: Opposing Counsel\nAnn Able for Pat\nann@x.test\nRole: Expert\nDr. Bo for Pat\nEND\n");
    CHECK(r.unknownFields.empty());
    CHECK(r.problems.empty());
}

TEST_CASE("Table rows repeat when Begin_ and End_ are in different rows")
{
    TempDir dir;
    const std::string table = "<w:tbl><w:tblPr/>"
                              "<w:tr>" + cell(para(txt("Name:"))) + cell(para(txt("Age:"))) + "</w:tr>"
                              "<w:tr>" + cell(para(mf("Begin_Children") + mf("Child_Name"))) + cell(para(mf("Child_Age"))) + "</w:tr>"
                              "<w:tr>" + cell(para(txt("School: ") + mf("Child_School"))) + cell(para(mf("End_Children"))) + "</w:tr>"
                              "</w:tbl>";
    const auto tmpl = makeDocx(dir, table + para(txt("after")));
    MergeData data;
    data.groups["children"] = {rec({{"childname", "Kim"}, {"childage", "11"}, {"childschool", "Elm"}}),
                               rec({{"childname", "Lee"}, {"childage", "7"}, {"childschool", "Oak"}})};
    const auto out = dir.path() / "out.docx";
    fillWordTemplate(tmpl, out, data, "");
    // The End_ cell keeps an (empty) paragraph: a table cell needs one.
    CHECK(textOf(entry(out, "word/document.xml")) ==
          "Name:\n|Age:\n|Kim\n|11\n|School: Elm\n|\n|Lee\n|7\n|School: Oak\n|\n|after\n");
    const std::string xml = entry(out, "word/document.xml");
    std::size_t rows = 0;
    for (std::size_t p = xml.find("<w:tr>"); p != std::string::npos; p = xml.find("<w:tr>", p + 1)) ++rows;
    CHECK(rows == 5);
}

TEST_CASE("Blocks inside one paragraph, nested blocks, and conditions")
{
    TempDir dir;
    const auto tmpl = makeDocx(
        dir, para(txt("Children: ") + mf("Begin_Children") + mf("Child_Name") + txt("; ") + mf("End_Children") + txt("end")) +
                 para(mf("Begin_IfChildren")) + para(txt("AND IN THE INTEREST OF")) +
                 para(mf("Begin_Children") + mf("Child_Name") + txt(" (") + mf("Child_Age") + txt(")") + mf("End_Children")) +
                 para(mf("End_IfChildren")) + para(mf("Begin_IfNoChildren") + txt("No children.") + mf("End_IfNoChildren")));
    MergeData data;
    data.groups["children"] = {rec({{"childname", "Kim"}, {"childage", "11"}}), rec({{"childname", "Lee"}, {"childage", "7"}})};
    data.groups["ifchildren"] = {MergeRecord{}};
    data.groups["ifnochildren"] = {};
    const auto out = dir.path() / "out.docx";
    const auto r = fillWordTemplate(tmpl, out, data, "");
    CHECK(textOf(entry(out, "word/document.xml")) ==
          "Children: Kim; Lee; end\nAND IN THE INTEREST OF\nKim (11)Lee (7)\n");  // one paragraph: repeats inline
    CHECK(r.problems.empty());

    data.groups["children"].clear();
    data.groups["ifchildren"].clear();
    data.groups["ifnochildren"] = {MergeRecord{}};
    fillWordTemplate(tmpl, out, data, "");
    CHECK(textOf(entry(out, "word/document.xml")) == "Children: end\nNo children.\n");
}

TEST_CASE("Headers are filled; a broken block is reported and the rest still filled")
{
    TempDir dir;
    const auto tmpl = makeDocx(dir, para(mf("Begin_OPC") + mf("OPC")) + para(mf("Cause")) + para(mf("End_Other")),
                               para(txt("Re: ") + mf("Cause \\* Upper")));
    MergeData data;
    data.fields = rec({{"cause", "d-1"}, {"opc", "Ann"}});
    const auto out = dir.path() / "out.docx";
    const auto r = fillWordTemplate(tmpl, out, data, "");
    CHECK(textOf(entry(out, "word/header1.xml")) == "Re: D-1\n");
    CHECK(textOf(entry(out, "word/document.xml")) == "Ann\nd-1\n");
    REQUIRE(r.problems.size() == 2);
    CHECK_THAT(r.problems[0], Catch::Matchers::ContainsSubstring("Begin_OPC"));
    CHECK_THAT(r.problems[1], Catch::Matchers::ContainsSubstring("End_Other"));
    CHECK(wordTemplateFields(tmpl) == std::vector<std::string>{"Begin_OPC", "OPC", "Cause", "End_Other"});
}

TEST_CASE("A block naming an unknown group is removed and reported; bad templates are refused")
{
    TempDir dir;
    const auto tmpl = makeDocx(dir, para(txt("a")) + para(mf("Begin_Typo")) + para(txt("b")) + para(mf("End_Typo")) + para(txt("c")));
    const auto out = dir.path() / "out.docx";
    const auto r = fillWordTemplate(tmpl, out, MergeData{}, "");
    CHECK(textOf(entry(out, "word/document.xml")) == "a\nc\n");
    CHECK(r.unknownGroups == std::vector<std::string>{"Typo"});

    CHECK_THROWS_WITH(fillWordTemplate(dir.path() / "gone.docx", out, MergeData{}, ""),
                      Catch::Matchers::ContainsSubstring("not found"));
    const auto junk = dir.touch("junk.docx", "not a zip");
    CHECK_THROWS_WITH(fillWordTemplate(junk, dir.path() / "x.docx", MergeData{}, ""),
                      Catch::Matchers::ContainsSubstring("Word"));
    CHECK_FALSE(std::filesystem::exists(dir.path() / "x.docx"));
}

TEST_CASE("Upper and lower case handle accented letters")
{
    CHECK(toUpperText("josé peña, ñ") == "JOSÉ PEÑA, Ñ");
    CHECK(toLowerText("ÉCOLE") == "école");
}

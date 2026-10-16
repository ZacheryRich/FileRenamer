#include <OpenXLSX.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <set>

#include "TestHelpers.h"
#include "finrenamer/VacationLetters.h"
#include "finrenamer/VacationSheet.h"

using namespace finrenamer;
using testing::TempDir;

// All data in this file is made up.

namespace {

const char* kSheetName = "ALL CASES (active & on hold)";

// Hand-built workbook: sheet 1 is an unrelated tab, sheet 2 is the case list with a table,
// shared strings (one rich text), an inline string, a multi-line cell, styles and a blank styled row.
std::vector<std::pair<std::string, std::string>> workbookEntries()
{
    const std::string ns = "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                           "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"";
    std::vector<std::pair<std::string, std::string>> e;
    e.push_back({"[Content_Types].xml",
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Types "
                 "xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"xml\" "
                 "ContentType=\"application/xml\"/></Types>"});
    e.push_back({"xl/workbook.xml",
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><workbook " + ns +
                     "><sheets><sheet name=\"Other\" sheetId=\"1\" r:id=\"rId1\"/><sheet name=\"ALL CASES (active &amp; on "
                     "hold)\" sheetId=\"2\" r:id=\"rId2\"/></sheets></workbook>"});
    e.push_back({"xl/_rels/workbook.xml.rels",
                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships "
                 "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" "
                 "Type=\"x/worksheet\" Target=\"worksheets/sheet1.xml\"/><Relationship Id=\"rId2\" Type=\"x/worksheet\" "
                 "Target=\"/xl/worksheets/sheet2.xml\"/></Relationships>"});
    e.push_back({"xl/sharedStrings.xml",
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><sst " + ns +
                     "><si><t> Client (Co-Counsel)</t></si><si><t>Attorney</t></si><si><t>OC 2 Address</t></si>"
                     "<si><t>County</t></si><si><t>Doe, Jane</t></si><si><r><t>Rich </t></r><r><t>Attorney</t></r></si>"
                     "<si><t xml:space=\"preserve\">Line one\nLine two</t></si><si><t>Travis</t></si></sst>"});
    e.push_back({"xl/worksheets/sheet1.xml",
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><worksheet " + ns +
                     "><sheetData><row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>unrelated</t></is></c></row></sheetData></worksheet>"});
    e.push_back({"xl/worksheets/sheet2.xml",
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><worksheet " + ns +
                     "><dimension ref=\"A1:D4\"/><sheetData>"
                     "<row r=\"1\" s=\"16\" customFormat=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c>"
                     "<c r=\"C1\" t=\"s\"><v>2</v></c><c r=\"D1\" t=\"s\"><v>3</v></c></row>"
                     "<row r=\"2\" ht=\"111.95\" customHeight=\"1\"><c r=\"A2\" s=\"18\" t=\"s\"><v>4</v></c>"
                     "<c r=\"B2\" s=\"18\" t=\"s\"><v>5</v></c><c r=\"C2\" s=\"18\" t=\"s\"><v>6</v></c>"
                     "<c r=\"D2\" s=\"18\" t=\"inlineStr\"><is><t>Travis County</t></is></c></row>"
                     "<row r=\"3\" ht=\"111.95\" customHeight=\"1\"/>"
                     "<row r=\"4\" ht=\"111.95\" customHeight=\"1\"/>"
                     "</sheetData><tableParts count=\"1\"><tablePart r:id=\"rId1\"/></tableParts></worksheet>"});
    e.push_back({"xl/worksheets/_rels/sheet2.xml.rels",
                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships "
                 "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" "
                 "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/table\" "
                 "Target=\"../tables/table1.xml\"/></Relationships>"});
    e.push_back({"xl/tables/table1.xml",
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><table " + ns +
                     " id=\"1\" name=\"Table1\" displayName=\"Table1\" ref=\"A1:D2\"><autoFilter ref=\"A1:D2\"/>"
                     "<tableColumns count=\"4\"><tableColumn id=\"1\" name=\" Client (Co-Counsel)\"/></tableColumns></table>"});
    return e;
}

struct Fixture {
    TempDir dir;
    std::filesystem::path book = dir.path() / "list.xlsx";
    Fixture() { testing::writeZip(book, workbookEntries()); }
};

std::string zipEntry(const std::filesystem::path& file, const std::string& name)
{
    OpenXLSX::XLZipArchive zip;
    zip.open(utf8FromPath(file));
    const std::string data = zip.hasEntry(name) ? zip.getEntry(name) : std::string("<missing>");
    zip.close();
    return data;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("normalizeHeader ignores case, spaces and punctuation")
{
    CHECK(normalizeHeader(" Client (Co-Counsel)") == "clientcocounsel");
    CHECK(normalizeHeader("Client_CoCounsel") == "clientcocounsel");
    CHECK(normalizeHeader("OPC 2 Address") == normalizeHeader("OC 2 Address"));
    CHECK(normalizeHeader("OPC") != normalizeHeader("OC 2 Address"));
}

TEST_CASE("The case list is read from the ALL CASES sheet")
{
    Fixture f;
    const auto sheet = loadVacationSheet(f.book);
    CHECK(sheet.sheetName == kSheetName);
    REQUIRE(sheet.headers.size() == 4);
    CHECK(sheet.headers[0] == "Client (Co-Counsel)");  // the leading space is dropped
    CHECK(sheet.column("opc 2 address") == 2);
    CHECK(sheet.column("nothing here") == -1);
    REQUIRE(sheet.rows.size() == 1);  // the blank rows are not cases
    CHECK(sheet.rows[0].row == 2);
    CHECK(sheet.rows[0].values[0] == "Doe, Jane");
    CHECK(sheet.rows[0].values[1] == "Rich Attorney");
    CHECK(sheet.rows[0].values[2] == "Line one\nLine two");
    CHECK(sheet.rows[0].values[3] == "Travis County");
}

TEST_CASE("Problems with the workbook are reported in plain words")
{
    TempDir dir;
    CHECK_THROWS_WITH(loadVacationSheet(dir.path() / "missing.xlsx"), Catch::Matchers::ContainsSubstring("not found"));
    const auto junk = dir.touch("junk.xlsx", "this is not a zip");
    CHECK_THROWS_WITH(loadVacationSheet(junk), Catch::Matchers::ContainsSubstring("Excel"));
}

TEST_CASE("Saving a case changes only the cells that changed")
{
    Fixture f;
    const auto before = loadVacationSheet(f.book);
    auto values = before.rows[0].values;
    values[3] = "Williamson County";
    values[2] = "Line one\nLine two\nLine three";
    const auto otherSheet = zipEntry(f.book, "xl/worksheets/sheet1.xml");
    const auto strings = zipEntry(f.book, "xl/sharedStrings.xml");

    CHECK(saveVacationRow(f.book, 2, before.rows[0].values, values) == 2);

    const auto after = loadVacationSheet(f.book);
    REQUIRE(after.rows.size() == 1);
    CHECK(after.rows[0].values == values);
    CHECK(zipEntry(f.book, "xl/worksheets/sheet1.xml") == otherSheet);  // the other tab is untouched
    CHECK(zipEntry(f.book, "xl/sharedStrings.xml") == strings);
    const auto sheetXml = zipEntry(f.book, "xl/worksheets/sheet2.xml");
    CHECK(contains(sheetXml, "<c r=\"A2\" s=\"18\" t=\"s\"><v>4</v></c>"));  // unchanged cells stay as they were
    CHECK(contains(sheetXml, "s=\"18\""));                                  // formatting kept on changed cells
    CHECK(contains(sheetXml, "customHeight=\"1\""));
    CHECK(contains(sheetXml, "<tableParts"));
    CHECK_FALSE(std::filesystem::exists(f.book.string() + ".saving"));
}

TEST_CASE("Clearing a cell leaves it empty")
{
    Fixture f;
    const auto before = loadVacationSheet(f.book);
    auto values = before.rows[0].values;
    values[1] = "";
    saveVacationRow(f.book, 2, before.rows[0].values, values);
    CHECK(loadVacationSheet(f.book).rows[0].values[1] == "");
}

TEST_CASE("A case changed by someone else is not overwritten")
{
    Fixture f;
    const auto mine = loadVacationSheet(f.book);
    auto theirs = mine.rows[0].values;
    theirs[1] = "Someone Else";
    saveVacationRow(f.book, 2, mine.rows[0].values, theirs);  // "someone else" saves first
    const auto bytes = testing::readFile(f.book);

    auto myEdit = mine.rows[0].values;
    myEdit[3] = "Bastrop County";
    CHECK_THROWS_AS(saveVacationRow(f.book, 2, mine.rows[0].values, myEdit), SheetConflict);
    CHECK(testing::readFile(f.book) == bytes);  // nothing was written
}

TEST_CASE("A new case goes on the first free row and the table grows to cover it")
{
    Fixture f;
    const auto sheet = loadVacationSheet(f.book);
    std::vector<std::string> row = {"Roe, Richard", "Travis", "", "Hays County"};
    CHECK(saveVacationRow(f.book, 0, {}, row) == 3);  // the blank styled row 3 is reused

    auto xml = zipEntry(f.book, "xl/worksheets/sheet2.xml");
    CHECK(contains(xml, "ht=\"111.95\""));
    auto table = zipEntry(f.book, "xl/tables/table1.xml");
    CHECK(contains(table, "ref=\"A1:D3\""));  // both the table and its filter
    CHECK_FALSE(contains(table, "A1:D2"));

    auto again = loadVacationSheet(f.book);
    REQUIRE(again.rows.size() == 2);
    CHECK(again.rows[1].row == 3);
    CHECK(again.rows[1].values == std::vector<std::string>{"Roe, Richard", "Travis", "", "Hays County"});

    // A row beyond what exists is created, with the dimension and table following.
    saveVacationRow(f.book, 0, {}, {"Poe, Pat", "", "", ""});
    saveVacationRow(f.book, 0, {}, {"Moe, Mo", "", "", ""});  // row 5 does not exist yet
    xml = zipEntry(f.book, "xl/worksheets/sheet2.xml");
    CHECK(contains(xml, "<row r=\"5\""));
    CHECK(contains(xml, "ref=\"A1:D5\""));
    CHECK(contains(zipEntry(f.book, "xl/tables/table1.xml"), "ref=\"A1:D5\""));
    CHECK(loadVacationSheet(f.book).rows.size() == 4);
}

TEST_CASE("An empty new case is refused")
{
    Fixture f;
    CHECK_THROWS(saveVacationRow(f.book, 0, {}, {"", "", "", ""}));
}

// ---- letters -------------------------------------------------------------------------------

namespace {

std::string field(const std::string& code, const std::string& shown)
{
    const std::string rpr = "<w:rPr><w:sz w:val=\"20\"/></w:rPr>";
    return "<w:r>" + rpr + "<w:fldChar w:fldCharType=\"begin\"/></w:r><w:r>" + rpr + "<w:instrText xml:space=\"preserve\"> " + code +
           " </w:instrText></w:r><w:r>" + rpr + "<w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:rPr><w:noProof/><w:sz "
           "w:val=\"20\"/></w:rPr><w:t>" + shown + "</w:t></w:r><w:r>" + rpr + "<w:fldChar w:fldCharType=\"end\"/></w:r>";
}

std::filesystem::path makeTemplate(const TempDir& dir)
{
    const std::string document =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:document "
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
        "<w:p><w:pPr><w:widowControl/></w:pPr>" + field("DATE \\@ \"MMMM d, yyyy\"", "January 1, 2000") + "</w:p>"
        "<w:p><w:r><w:t>Via eService: </w:t></w:r>" + field("MERGEFIELD eService_Email", "«eService_Email»") + "</w:p>"
        "<w:p>" + field("MERGEFIELD OPC_Address", "«OPC_Address»") + "</w:p>"
        "<w:p>" + field("MERGEFIELD OPC_2_Address", "«OPC_2_Address»") + "</w:p>"
        "<w:p><w:r><w:t xml:space=\"preserve\">Re: </w:t></w:r>" + field("MERGEFIELD \"County\"", "«County»") +
        "<w:r><w:t xml:space=\"preserve\"> County</w:t></w:r></w:p>"
        "<w:p>" + field("MERGEFIELD Client_CoCounsel", "«Client_CoCounsel»") + "</w:p>"
        "<w:p>" + field("MERGEFIELD Not_A_Column", "«Not_A_Column»") + "</w:p>"
        "<w:sectPr><w:headerReference w:type=\"default\" r:id=\"rId8\"/><w:pgSz w:w=\"12240\" w:h=\"15840\"/></w:sectPr>"
        "</w:body></w:document>";
    const auto file = dir.path() / "template.docx";
    testing::writeZip(
        file,
        {{"[Content_Types].xml",
          "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
          "<Override PartName=\"/word/document.xml\" ContentType=\"x\"/>"
          "<Override PartName=\"/word/recipientData.xml\" ContentType=\"y\"/></Types>"},
         {"word/document.xml", document},
         {"word/header1.xml", "<w:hdr/>"},
         {"word/settings.xml",
          "<w:settings><w:zoom w:percent=\"100\"/><w:mailMerge><w:linkToQuery/><w:connectString w:val=\"C:\\Dropbox\\x.xlsx\"/>"
          "</w:mailMerge><w:defaultTabStop w:val=\"720\"/></w:settings>"},
         {"word/_rels/settings.xml.rels", "<Relationships><Relationship Id=\"r\" Type=\"mailMergeSource\" Target=\"file:///C:/x.xlsx\"/></Relationships>"},
         {"word/recipientData.xml", "<recipients/>"}});
    return file;
}

LetterValues oneLetter(const std::string& county, const std::string& address)
{
    return {{"eservicemail", "unused"},  // a column the template doesn't use
            {"eserviceemail", "a@example.test"},
            {"opcaddress", address},
            {"opc2address", ""},
            {"county", county},
            {"clientcocounsel", "Doe, Jane & Roe <Richard>"}};
}

}  // namespace

TEST_CASE("The template's merge fields are listed in order")
{
    TempDir dir;
    const auto names = templateMergeFields(makeTemplate(dir));
    CHECK(names == std::vector<std::string>{"eService_Email", "OPC_Address", "OPC_2_Address", "County", "Client_CoCounsel", "Not_A_Column"});
    CHECK_THROWS_WITH(templateMergeFields(dir.path() / "gone.docx"), Catch::Matchers::ContainsSubstring("not found"));
}

TEST_CASE("Letters are merged into one document, one per page")
{
    TempDir dir;
    const auto tmpl = makeTemplate(dir);
    const auto out = dir.path() / "Letters.docx";
    const auto before = testing::readFile(tmpl);

    const auto result = writeCombinedLetters(
        tmpl, out, {oneLetter("Travis", "100 Main St.\nAustin, TX 78701"), oneLetter("Hays", "5 Oak Ln.")}, "October 14, 2026");
    CHECK(result.letters == 2);
    CHECK(result.missingColumns == std::vector<std::string>{"Not_A_Column"});
    CHECK(testing::readFile(tmpl) == before);  // the template is only read
    CHECK_FALSE(std::filesystem::exists(out.string() + ".saving"));

    const auto xml = zipEntry(out, "word/document.xml");
    CHECK(contains(xml, "Travis"));
    CHECK(contains(xml, "Hays"));
    CHECK(contains(xml, "100 Main St.</w:t><w:br/><w:t xml:space=\"preserve\">Austin, TX 78701"));  // a line break in the cell
    CHECK(contains(xml, "Doe, Jane &amp; Roe &lt;Richard&gt;"));                                    // escaped
    CHECK_FALSE(contains(xml, "MERGEFIELD"));
    CHECK_FALSE(contains(xml, "«"));
    CHECK(contains(xml, "<w:sz w:val=\"20\"/>"));  // the placeholder's font size carries over

    // The date field is still a field but shows today's date; letter 1 doesn't start with a page break, letter 2 does.
    CHECK(contains(xml, "DATE \\@"));
    CHECK(contains(xml, "October 14, 2026"));
    CHECK_FALSE(contains(xml, "January 1, 2000"));
    std::size_t breaks = 0;
    for (std::size_t p = 0; (p = xml.find("<w:pageBreakBefore/>", p)) != std::string::npos; ++p) ++breaks;
    CHECK(breaks == 1);
    CHECK(xml.find("<w:pageBreakBefore/>") > xml.find("Travis"));
    // After widowControl's sibling? the break goes in front of it (schema order).
    CHECK(contains(xml, "<w:pPr><w:pageBreakBefore/><w:widowControl/></w:pPr>"));
    // One section: the page setup and header are the template's.
    CHECK(contains(xml, "<w:headerReference"));
    std::size_t sections = 0;
    for (std::size_t p = 0; (p = xml.find("<w:sectPr", p)) != std::string::npos; ++p) ++sections;
    CHECK(sections == 1);

    // The link to the data source is gone.
    CHECK_FALSE(contains(zipEntry(out, "word/settings.xml"), "mailMerge"));
    CHECK(contains(zipEntry(out, "word/settings.xml"), "defaultTabStop"));
    CHECK(zipEntry(out, "word/_rels/settings.xml.rels") == "<missing>");
    CHECK(zipEntry(out, "word/recipientData.xml") == "<missing>");
    CHECK_FALSE(contains(zipEntry(out, "[Content_Types].xml"), "recipientData"));
    CHECK(contains(zipEntry(out, "word/header1.xml"), "<w:hdr/>"));
}

namespace {

std::string pngHeader(int w, int h)
{
    auto be = [](int v) { return std::string{char(v >> 24), char(v >> 16), char(v >> 8), char(v)}; };
    return std::string("\x89PNG\r\n\x1a\n", 8) + be(13) + "IHDR" + be(w) + be(h) + std::string("\x08\x06\0\0\0", 5);
}

std::string jpegHeader(int w, int h)
{
    auto be16 = [](int v) { return std::string{char(v >> 8), char(v)}; };
    return std::string("\xFF\xD8", 2) + std::string("\xFF\xE0", 2) + be16(16) + std::string(14, '\0') +
           std::string("\xFF\xC0", 2) + be16(11) + std::string("\x08", 1) + be16(h) + be16(w) + std::string("\x01\x01\x11\x00", 4);
}

std::filesystem::path makeSignedTemplate(const TempDir& dir)
{
    const std::string document =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:document "
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
        "<w:p><w:r><w:t>Very truly yours,</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:ind w:left=\"4320\"/></w:pPr>" + field("MERGEFIELD Signature", "«Signature»") + "</w:p>"
        "<w:p>" + field("MERGEFIELD \"County\"", "«County»") + "</w:p>"
        "<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/></w:sectPr></w:body></w:document>";
    const auto file = dir.path() / "signed.docx";
    testing::writeZip(
        file,
        {{"[Content_Types].xml",
          "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
          "<Default Extension=\"xml\" ContentType=\"application/xml\"/></Types>"},
         {"word/document.xml", document},
         {"word/_rels/document.xml.rels",
          "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
          "<Relationship Id=\"rId1\" Type=\"x\" Target=\"styles.xml\"/></Relationships>"}});
    return file;
}

}  // namespace

TEST_CASE("Image sizes are read from PNG and JPEG headers")
{
    TempDir dir;
    const auto png = dir.touch("a.png", pngHeader(600, 200));
    const auto jpg = dir.touch("b.jpg", jpegHeader(300, 150));
    CHECK(imageSize(png).width == 600);
    CHECK(imageSize(png).height == 200);
    CHECK(imageSize(jpg).width == 300);
    CHECK(imageSize(jpg).height == 150);
    CHECK_THROWS_WITH(imageSize(dir.touch("c.png", "not a picture")), Catch::Matchers::ContainsSubstring("PNG or JPEG"));
    CHECK_THROWS_WITH(imageSize(dir.path() / "gone.png"), Catch::Matchers::ContainsSubstring("could not be opened"));
}

TEST_CASE("A signature picture replaces the Signature merge field")
{
    TempDir dir;
    const auto tmpl = makeSignedTemplate(dir);
    const auto sigA = dir.touch("jane.png", pngHeader(1000, 250));  // wide: limited by the 2.2 inch width
    const auto sigB = dir.touch("sam.jpg", jpegHeader(200, 200));   // square: limited by the 0.8 inch height
    const auto out = dir.path() / "Letters.docx";

    const auto result = writeCombinedLetters(
        tmpl, out, {LetterValues{{"county", "Travis"}}, LetterValues{{"county", "Hays"}}, LetterValues{{"county", "Bell"}},
                    LetterValues{{"county", "Webb"}}},
        "d", {sigA, sigB, {}, sigA});
    CHECK(result.letters == 4);
    CHECK(result.signaturesAdded == 3);
    CHECK(result.templateHasSignatureField);
    CHECK(result.missingColumns.empty());  // "Signature" is not a sheet column

    const auto xml = zipEntry(out, "word/document.xml");
    CHECK_FALSE(contains(xml, "MERGEFIELD"));
    std::size_t pictures = 0;
    for (std::size_t p = 0; (p = xml.find("<w:drawing>", p)) != std::string::npos; ++p) ++pictures;
    CHECK(pictures == 3);
    CHECK(contains(xml, "cx=\"2011680\" cy=\"502920\""));  // 2.2 in x 0.55 in
    CHECK(contains(xml, "cx=\"731520\" cy=\"731520\""));   // 0.8 in square
    CHECK(contains(xml, "r:embed=\"rIdSignature1\""));
    CHECK(contains(xml, "r:embed=\"rIdSignature2\""));
    CHECK_FALSE(contains(xml, "r:embed=\"rIdSignature3\""));  // the same picture is stored once

    // The pictures, their relationships and content types are in the package.
    CHECK(zipEntry(out, "word/media/signature1.png") == pngHeader(1000, 250));
    CHECK(zipEntry(out, "word/media/signature2.jpeg") == jpegHeader(200, 200));
    const auto rels = zipEntry(out, "word/_rels/document.xml.rels");
    CHECK(contains(rels, "Id=\"rIdSignature1\""));
    CHECK(contains(rels, "Target=\"media/signature2.jpeg\""));
    CHECK(contains(rels, "Id=\"rId1\""));  // the existing ones stay
    const auto types = zipEntry(out, "[Content_Types].xml");
    CHECK(contains(types, "Extension=\"png\" ContentType=\"image/png\""));
    CHECK(contains(types, "Extension=\"jpeg\" ContentType=\"image/jpeg\""));

    // Every picture id in the document is unique.
    std::set<std::string> ids;
    for (std::size_t p = 0; (p = xml.find("<wp:docPr id=\"", p)) != std::string::npos; p += 10) ids.insert(xml.substr(p + 14, 6));
    CHECK(ids.size() == 3);
}

TEST_CASE("The signer is found from the Partner on case column, then Attorney")
{
    const std::vector<SignatureEntry> list{{"Jane Doe", "jane.png"}, {"Sam Roe", "sam.png"}};
    CHECK(signerName({{"partneroncase", " Jane Doe: "}, {"attorney", "Sam Roe"}}) == "Jane Doe:");
    CHECK(signerName({{"partneroncase", "  "}, {"attorney", "Sam Roe"}}) == "Sam Roe");
    CHECK(signerName({{"county", "Travis"}}).empty());
    CHECK(signatureFor(list, "Jane Doe:") == std::filesystem::path("jane.png"));
    CHECK(signatureFor(list, "sam  ROE") == std::filesystem::path("sam.png"));
    CHECK(signatureFor(list, "Pat Poe").empty());
    CHECK(signatureFor(list, "").empty());
}

TEST_CASE("Letters without pictures or without a Signature field are made as before")
{
    TempDir dir;
    const auto out = dir.path() / "Plain.docx";
    const auto plain = writeCombinedLetters(makeTemplate(dir), out, {oneLetter("Travis", "x")}, "d",
                                            {dir.touch("s.png", pngHeader(10, 10))});
    CHECK(plain.signaturesAdded == 0);  // the template has no Signature field
    CHECK_FALSE(plain.templateHasSignatureField);
    CHECK(zipEntry(out, "word/media/signature1.png") == "<missing>");

    const auto signed_ = writeCombinedLetters(makeSignedTemplate(dir), out, {LetterValues{{"county", "A"}}}, "d");
    CHECK(signed_.signaturesAdded == 0);
    CHECK(signed_.templateHasSignatureField);  // the field is left blank
    CHECK_FALSE(contains(zipEntry(out, "word/document.xml"), "<w:drawing>"));

    // An unreadable picture stops the letters and leaves nothing behind.
    CHECK_THROWS_WITH(writeCombinedLetters(makeSignedTemplate(dir), dir.path() / "Bad.docx", {LetterValues{}}, "d",
                                           {dir.touch("bad.png", "junk")}),
                      Catch::Matchers::ContainsSubstring("PNG or JPEG"));
    CHECK_FALSE(std::filesystem::exists(dir.path() / "Bad.docx"));
    CHECK_FALSE(std::filesystem::exists(dir.path() / "Bad.docx.saving"));
}

TEST_CASE("Making letters needs cases and a readable template")
{
    TempDir dir;
    const auto tmpl = makeTemplate(dir);
    CHECK_THROWS_WITH(writeCombinedLetters(tmpl, dir.path() / "x.docx", {}, "d"), Catch::Matchers::ContainsSubstring("No cases"));
    const auto junk = dir.touch("bad.docx", "not a zip");
    CHECK_THROWS_WITH(writeCombinedLetters(junk, dir.path() / "x.docx", {oneLetter("a", "b")}, "d"),
                      Catch::Matchers::ContainsSubstring("Word"));
    CHECK_FALSE(std::filesystem::exists(dir.path() / "x.docx"));
}

TEST_CASE("Letter values come from the sheet's columns")
{
    Fixture f;
    const auto sheet = loadVacationSheet(f.book);
    const auto v = letterValues(sheet, sheet.rows[0]);
    CHECK(v.at("clientcocounsel") == "Doe, Jane");
    CHECK(v.at("opc2address") == "Line one\nLine two");  // "OC 2 Address" is found as OPC 2 Address
}

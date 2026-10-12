#include <OpenXLSX.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "finrenamer/BatesLog.h"
#include "finrenamer/BatesSheet.h"
#include "finrenamer/DirectoryListing.h"

using namespace finrenamer;
using testing::TempDir;

namespace {

// Column A, B, C of the first sheet as text, plus whether A is bold, one entry per row 1..last.
struct SheetRow {
    std::string a, b, c;
    bool boldA = false;
    bool grey = false;
};

std::vector<SheetRow> readSheet(const std::filesystem::path& file)
{
    OpenXLSX::XLDocument doc;
    doc.open(utf8FromPath(file));
    auto ws = doc.workbook().worksheet(1);
    auto& styles = doc.styles();
    auto text = [&](uint32_t r, uint16_t c) -> std::string {
        const auto cell = ws.findCell(r, c);
        if (!cell || cell.value().type() == OpenXLSX::XLValueType::Empty) return {};
        return cell.value().get<std::string>();
    };
    std::vector<SheetRow> rows;
    for (uint32_t r = 1; r <= ws.rowCount(); ++r) {
        SheetRow row{text(r, 1), text(r, 2), text(r, 3), false, false};
        const auto cell = ws.findCell(r, 1);
        if (cell) {
            const auto cf = styles.cellFormats()[cell.cellFormat()];
            row.boldA = styles.fonts()[cf.fontIndex()].bold();
            row.grey = cf.fillIndex() != 0;
        }
        rows.push_back(row);
    }
    doc.close();
    return rows;
}

std::vector<std::string> namesOnly(const std::vector<SheetRow>& rows)
{
    std::vector<std::string> out;
    for (const auto& r : rows) out.push_back(r.a);
    return out;
}

// The folder tree of the user's sample "Bates Log.xlsx".
void makeSampleTree(const TempDir& dir)
{
    for (const char* f : {"2024.07.01 Test.pdf", "This is a Test.pdf", "Folder 1/Bates Video Placeholder.pdf", "Folder 1/DEF.pdf",
                          "Folder 1/test.mp4", "Folder 1/Sub 1/AAAAA.pdf", "Folder 1/Sub 1/ABC.pdf", "Folder 1/Sub 1/Bates 356.pdf",
                          "Folder 2/GH.pdf", "Folder 2/Tester -1.pdf", "Folder 2/Tester.pdf", "Folder 2/aaaaa.pdf", "Folder 2/test2.m4a"})
        dir.touch(f);
}

const std::vector<std::string> kSampleNames = {
    "Name", "",
    "/", "2024.07.01 Test.pdf", "This is a Test.pdf", "",
    "/Folder 1", "Bates Video Placeholder.pdf", "DEF.pdf", "test.mp4", "",
    "/Folder 1/Sub 1", "AAAAA.pdf", "ABC.pdf", "Bates 356.pdf", "",
    "/Folder 2", "GH.pdf", "Tester -1.pdf", "Tester.pdf", "aaaaa.pdf", "test2.m4a"};

}  // namespace

TEST_CASE("The directory listing follows the sample: root first, depth first, plain character order")
{
    TempDir dir;
    makeSampleTree(dir);
    dir.touch("Folder 1/Sub 1/Deeper/note.txt");
    std::filesystem::create_directories(dir.path() / "Empty Folder" / "Also Empty");

    const auto listing = listDirectoryTree(dir.path());
    std::vector<std::string> folders;
    for (const auto& f : listing) folders.push_back(f.folder);
    CHECK(folders == std::vector<std::string>{"/", "/Folder 1", "/Folder 1/Sub 1", "/Folder 1/Sub 1/Deeper", "/Folder 2"});
    CHECK(listing[0].files == std::vector<std::string>{"2024.07.01 Test.pdf", "This is a Test.pdf"});
    CHECK(listing[4].files == std::vector<std::string>{"GH.pdf", "Tester -1.pdf", "Tester.pdf", "aaaaa.pdf", "test2.m4a"});
}

TEST_CASE("The spreadsheet being written is left out of the listing")
{
    TempDir dir;
    dir.touch("a.pdf");
    const auto xlsx = dir.touch("Bates Log.XLSX", "x");
    const auto listing = listDirectoryTree(dir.path(), xlsx);
    REQUIRE(listing.size() == 1);
    CHECK(listing[0].files == std::vector<std::string>{"a.pdf"});
    CHECK(listDirectoryTree(dir.path())[0].files.size() == 2);
}

TEST_CASE("Office lock files and Thumbs.db are not listed")
{
    TempDir dir;
    dir.touch("a.pdf");
    dir.touch("~$a.docx");
    dir.touch("Thumbs.db");
    dir.touch("desktop.ini");
    const auto listing = listDirectoryTree(dir.path());
    REQUIRE(listing.size() == 1);
    CHECK(listing[0].files == std::vector<std::string>{"a.pdf"});
}

TEST_CASE("A new spreadsheet has the sample's layout")
{
    TempDir dir;
    makeSampleTree(dir);
    const auto xlsx = dir.path() / "out" / "Bates Log.xlsx";
    std::filesystem::create_directories(xlsx.parent_path());
    const auto folder = dir.path();

    const auto result = writeListing(xlsx, listDirectoryTree(folder));
    // The spreadsheet itself lives outside the listed folder here; list again below.
    CHECK(result.created);
    CHECK(result.folders == 4);
    CHECK(result.files == 13);

    const auto rows = readSheet(xlsx);
    CHECK(namesOnly(rows) == kSampleNames);
    CHECK(rows[0].boldA);
    CHECK(rows[0].grey);
    CHECK(rows[0].b == "Begin Bates");
    CHECK(rows[0].c == "End Bates");
    CHECK(rows[2].boldA);       // "/"
    CHECK_FALSE(rows[3].boldA);  // a file
    CHECK(rows[6].boldA);       // "/Folder 1"
    CHECK_FALSE(rows[6].grey);
}

TEST_CASE("An existing spreadsheet gets the new listing added at the bottom")
{
    TempDir dir;
    makeSampleTree(dir);
    const auto xlsx = dir.path() / "Bates Log.xlsx";
    const auto tree = listDirectoryTree(dir.path());

    writeListing(xlsx, tree);
    const auto second = writeListing(xlsx, tree);
    CHECK_FALSE(second.created);
    CHECK(second.firstRow == kSampleNames.size() + 2);

    const auto rows = readSheet(xlsx);
    auto expected = kSampleNames;
    expected.push_back("");
    expected.insert(expected.end(), kSampleNames.begin() + 2, kSampleNames.end());  // no second header
    CHECK(namesOnly(rows) == expected);
    CHECK(second.lastRow == rows.size());
}

TEST_CASE("Bates numbers are matched by name, ignoring .pdf and letter case")
{
    TempDir dir;
    makeSampleTree(dir);
    const auto xlsx = dir.path() / "log.xlsx";
    writeListing(xlsx, listDirectoryTree(dir.path()));

    const auto log = parseBatesLogText(
        "AAAAA\tSMITH000001\tSMITH000004\r\n"
        "abc.PDF\tSMITH000005\tSMITH000009\r\n"
        "Tester.pdf\tSMITH000010\tSMITH000010\r\n"
        "Not In Sheet.pdf\tSMITH000011\tSMITH000012\r\n");
    CHECK(log.lines == 4);

    const auto result = applyBatesLog(xlsx, log);
    CHECK(result.fileRows == 13);
    CHECK(result.matched == 4);  // "AAAAA" also matches the row "aaaaa.pdf": case is ignored
    CHECK(result.unmatchedNames.size() == 9);
    CHECK(result.unusedLogNames == std::vector<std::string>{"Not In Sheet.pdf"});
    CHECK(result.sharedNames == std::vector<std::string>{"aaaaa.pdf"});  // said so, so the user can check

    const auto rows = readSheet(xlsx);
    CHECK(rows[12].a == "AAAAA.pdf");
    CHECK(rows[12].b == "SMITH000001");
    CHECK(rows[12].c == "SMITH000004");
    CHECK(rows[13].b == "SMITH000005");
    CHECK(rows[19].a == "Tester.pdf");
    CHECK(rows[19].c == "SMITH000010");
    CHECK(rows[3].b.empty());  // unmatched rows are left alone
    CHECK(rows[2].b.empty());  // folder rows too
    CHECK(namesOnly(rows) == kSampleNames);  // column A untouched
}

TEST_CASE("A file name on two rows gets the same numbers and is reported")
{
    TempDir dir;
    dir.touch("One/same.pdf");
    dir.touch("Two/same.pdf");
    const auto xlsx = dir.path() / "log.xlsx";
    writeListing(xlsx, listDirectoryTree(dir.path()));
    const auto result = applyBatesLog(xlsx, parseBatesLogText("same.pdf\tA1\tA2\n"));
    CHECK(result.matched == 2);
    CHECK(result.sharedNames == std::vector<std::string>{"same.pdf"});
}

TEST_CASE("A sheet made without Bates headers gets them, and numbers already there stay")
{
    TempDir dir;
    const auto xlsx = dir.path() / "mine.xlsx";
    {
        OpenXLSX::XLDocument doc;
        doc.create(utf8FromPath(xlsx), OpenXLSX::XLForceOverwrite);
        auto ws = doc.workbook().worksheet(1);
        ws.cell(1, 1).value() = "Name";
        ws.cell(3, 1).value() = "/";
        ws.cell(4, 1).value() = "a.pdf";
        ws.cell(4, 2).value() = "OLD1";
        ws.cell(4, 3).value() = "OLD2";
        ws.cell(5, 1).value() = "b.pdf";
        doc.save();
        doc.close();
    }
    const auto result = applyBatesLog(xlsx, parseBatesLogText("b.pdf\tB1\tB2\n"));
    CHECK(result.matched == 1);
    const auto rows = readSheet(xlsx);
    CHECK(rows[0].b == "Begin Bates");
    CHECK(rows[0].c == "End Bates");
    CHECK(rows[3].b == "OLD1");
    CHECK(rows[4].b == "B1");
}

TEST_CASE("A plain file list has no Bates headers, and a log added later brings them")
{
    TempDir dir;
    dir.touch("sub/a.pdf");
    dir.touch("b.pdf");
    const auto xlsx = dir.path() / "list.xlsx";
    writeListing(xlsx, listDirectoryTree(dir.path(), xlsx), /*batesColumns=*/false);

    auto rows = readSheet(xlsx);
    REQUIRE(rows.size() >= 3);
    CHECK(rows[0].a == "Name");
    CHECK(rows[0].b.empty());
    CHECK(rows[0].c.empty());

    // A second plain listing appended to the same sheet still adds no Bates headers.
    writeListing(xlsx, listDirectoryTree(dir.path(), xlsx), false);
    rows = readSheet(xlsx);
    CHECK(rows[0].b.empty());

    // Adding a log later puts the two headers in.
    applyBatesLog(xlsx, parseBatesLogText("b.pdf\tB1\tB2\n"));
    rows = readSheet(xlsx);
    CHECK(rows[0].b == "Begin Bates");
    CHECK(rows[0].c == "End Bates");
}

TEST_CASE("The Bates log reader handles odd encodings and layouts")
{
    SECTION("UTF-16 with a byte-order mark")
    {
        TempDir dir;
        std::string bytes = "\xFF\xFE";
        for (char c : std::string("Caf\xC3\xA9.pdf\tX1\tX2\r\n")) {
            if (static_cast<unsigned char>(c) == 0xC3) { bytes += "\xE9"; bytes += '\0'; continue; }
            if (static_cast<unsigned char>(c) == 0xA9) continue;
            bytes += c; bytes += '\0';
        }
        const auto file = dir.touch("log.log", bytes);
        const auto log = parseBatesLog(file);
        REQUIRE(log.lines == 1);
        CHECK(log.entries.count(matchKey("Caf\xC3\xA9")) == 1);
    }
    SECTION("UTF-8 with a BOM, blank lines, a line without numbers, a repeated name")
    {
        const auto log = parseBatesLogText(
            "\n"
            "a.pdf\t1\t2\n"
            "\n"
            "no numbers here.pdf\n"
            "A.PDF\t3\t4\n");
        CHECK(log.lines == 2);
        CHECK(log.duplicates == 1);
        CHECK(log.entries.at(matchKey("a")).begin == "3");  // the last one wins
    }
    SECTION("Missing file")
    {
        CHECK_THROWS(parseBatesLog("/no/such/file.log"));
    }
}

TEST_CASE("matchKey")
{
    CHECK(matchKey("  Smith 001.PDF ") == matchKey("smith 001"));
    CHECK(matchKey("a.pdf") != matchKey("b.pdf"));
    CHECK(matchKey("report.pdf.pdf") == matchKey("report.pdf") + ".pdf");  // only one ".pdf" is dropped
}

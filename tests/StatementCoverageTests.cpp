#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <map>

#include "TestHelpers.h"
#include "finrenamer/Database.h"
#include "finrenamer/DeficiencyReport.h"
#include "finrenamer/StatementCoverage.h"

using namespace finrenamer;
namespace fs = std::filesystem;
namespace chr = std::chrono;
using Catch::Matchers::ContainsSubstring;

namespace {

Month ym(int year, unsigned month)
{
    return Month{chr::year{year}, chr::month{month}};
}

std::vector<unsigned> months(std::initializer_list<unsigned> m) { return m; }

// A case with a checking account (H), a brokerage account (W) and files in a temp folder.
struct Coverage {
    testing::TempDir dir;
    Database db = Database::openInMemory();
    std::int64_t caseId = db.createCase({0, "Smith & Jones", ""}, defaultCasePeople());
    std::int64_t husband = db.listPeople(caseId)[0].id;
    std::int64_t wife = db.listPeople(caseId)[1].id;
    std::int64_t checking = db.createAccount({0, caseId, "Chase", "", "Checking", "1234", {husband}});
    std::int64_t brokerage = db.createAccount({0, caseId, "Fidelity", "", "IRA", "5678", {wife}});

    CoverageScan scan(bool subfolders = true)
    {
        return scanStatements({dir.path()}, subfolders, db.loadAccounts(caseId), db.oldAccountNames(caseId));
    }
    std::set<Month> monthsOf(const CoverageScan& s, std::int64_t id)
    {
        const auto found = s.covered.find(id);
        return found == s.covered.end() ? std::set<Month>{} : found->second;
    }
};

}  // namespace

TEST_CASE("A date covers the months it accounts for")
{
    CHECK(monthsCovered(SingleDate{makeDate(2024, 1, 31)}) == std::vector<Month>{ym(2024, 1)});
    CHECK(monthsCovered(Quarter{2024, 4}) == std::vector<Month>{ym(2024, 10), ym(2024, 11), ym(2024, 12)});
    CHECK(monthsCovered(Period{makeDate(2023, 11, 16), makeDate(2024, 1, 15)}) ==
          std::vector<Month>{ym(2023, 11), ym(2023, 12), ym(2024, 1)});
    CHECK(monthsCovered(Period{makeDate(2024, 3, 1), makeDate(2024, 3, 31)}) == std::vector<Month>{ym(2024, 3)});
}

TEST_CASE("Month lists are written as short ranges")
{
    CHECK(monthListText({}) == "");
    CHECK(monthListText(months({5})) == "May");
    CHECK(monthListText(months({1, 2})) == "Jan, Feb");
    CHECK(monthListText(months({1, 2, 3})) == "Jan\xE2\x80\x93Mar");
    CHECK(monthListText(months({1, 2, 3, 5, 8, 9, 10, 11, 12})) ==
          "Jan\xE2\x80\x93Mar, May, Aug\xE2\x80\x93" "Dec");
    CHECK(monthListText(months({2, 4, 6})) == "Feb, Apr, Jun");
    CHECK(longDateText(makeDate(2022, 3, 5)) == "March 5, 2022");
    CHECK(monthYearText(ym(2024, 9)) == "September 2024");
    CHECK(monthIndex(monthFromIndex(24290)) == 24290);
}

TEST_CASE("Months found come from renamed files, wherever they are")
{
    Coverage c;
    c.dir.touch("2024.01.31 Chase Checking 1234 (H).pdf");
    c.dir.touch("2024.02.29 Chase Checking 1234 (H).pdf");
    c.dir.touch("2023/2023.12.31 Chase Checking 1234 (H).pdf");  // a subfolder
    c.dir.touch("2024.02.29 Chase Checking 1234 (H) (2).pdf");   // a collision copy: same month
    c.dir.touch("2024.Q1 Fidelity IRA 5678 (W).pdf");            // a quarter: Jan, Feb, Mar
    c.dir.touch("2024.04.01 - 2024.06.30 Fidelity IRA 5678 (W).PDF");  // a period, any letter case

    auto scan = c.scan();
    CHECK(c.monthsOf(scan, c.checking) == std::set<Month>{ym(2023, 12), ym(2024, 1), ym(2024, 2)});
    CHECK(c.monthsOf(scan, c.brokerage) ==
          std::set<Month>{ym(2024, 1), ym(2024, 2), ym(2024, 3), ym(2024, 4), ym(2024, 5), ym(2024, 6)});
    CHECK(scan.pdfCount == 6);
    CHECK(scan.matchedCount == 6);
    CHECK(scan.unmatched.empty());

    scan = c.scan(false);  // top folder only
    CHECK(c.monthsOf(scan, c.checking) == std::set<Month>{ym(2024, 1), ym(2024, 2)});
}

TEST_CASE("Files that aren't statements of this case are reported, not counted")
{
    Coverage c;
    c.dir.touch("scan0001.pdf");
    c.dir.touch("2024.01.31 Wells Fargo Savings 9999 (H).pdf");  // not an account of this case
    c.dir.touch("2024.13.31 Chase Checking 1234 (H).pdf");       // not a date
    c.dir.touch("2024.03.31 Chase Checking 1234 (H).pdf");
    c.dir.touch("notes.txt");

    const auto scan = c.scan();
    CHECK(scan.pdfCount == 4);
    CHECK(scan.matchedCount == 1);
    CHECK(scan.unmatched.size() == 3);
    CHECK(c.monthsOf(scan, c.checking) == std::set<Month>{ym(2024, 3)});
}

TEST_CASE("Names from before a typo fix or a new number still count for the account")
{
    Coverage c;
    AccountRecord r = *c.db.getAccount(c.checking);
    r.institution = "Chsae";
    r.institutionDisplay = "";  // the display name follows the institution
    c.db.updateAccount(r);      // the typo...
    r.institution = "Chase";
    c.db.updateAccount(r);      // ...fixed
    c.dir.touch("2024.01.31 Chsae Checking 1234 (H).pdf");

    r = *c.db.getAccount(c.checking);
    r.previousLastFour = {r.lastFour};
    r.lastFour = "7777";  // a replacement card
    c.db.updateAccount(r);
    c.dir.touch("2024.02.29 Chase Checking 1234 (H).pdf");  // statement showing the old number
    c.dir.touch("2024.03.31 Chase Checking 7777 (H).pdf");

    CHECK(c.monthsOf(c.scan(), c.checking) == std::set<Month>{ym(2024, 1), ym(2024, 2), ym(2024, 3)});
}

TEST_CASE("A combined statement counts for every account on it")
{
    Coverage c;
    const auto savings = c.db.createAccount({0, c.caseId, "Chase", "", "Savings", "2222", {c.husband}});
    AccountRecord combined;
    combined.caseId = c.caseId;
    combined.ownerIds = {c.husband};
    combined.memberIds = {c.checking, savings};
    c.db.createAccount(combined);

    c.dir.touch("2024.05.31 Chase Checking x1234, Savings x2222 (H).pdf");
    c.dir.touch("2024.06.30 Chase Savings 2222 (H).pdf");  // a single statement of one member

    const auto scan = c.scan();
    CHECK(c.monthsOf(scan, c.checking) == std::set<Month>{ym(2024, 5)});
    CHECK(c.monthsOf(scan, savings) == std::set<Month>{ym(2024, 5), ym(2024, 6)});
    CHECK(scan.unmatched.empty());
}

TEST_CASE("Coverage is worked out per year within the range")
{
    Coverage c;
    for (const char* name : {"2023.11.30", "2023.12.31", "2024.01.31", "2024.03.31", "2024.04.30"})
        c.dir.touch(std::string(name) + " Chase Checking 1234 (H).pdf");
    const auto scan = c.scan();

    const auto accounts = c.db.loadAccounts(c.caseId);
    std::vector<Account> checking;
    for (const Account& a : accounts)
        if (a.id == c.checking) checking.push_back(a);

    // Range: Oct 2023 to Apr 2024 (no opening/closing dates).
    auto coverage = analyzeCoverage(checking, scan, ym(2023, 10), ym(2024, 4));
    REQUIRE(coverage.size() == 1);
    REQUIRE(coverage[0].years.size() == 2);
    CHECK(coverage[0].label == "Chase Checking 1234 (H)");
    CHECK(coverage[0].years[0].year == 2023);
    CHECK(coverage[0].years[0].found == months({11, 12}));
    CHECK(coverage[0].years[0].missing == months({10}));  // months before the range aren't listed
    CHECK(coverage[0].years[1].found == months({1, 3, 4}));
    CHECK(coverage[0].years[1].missing == months({2}));   // nor months after it

    // A range that ends before it starts has nothing in it.
    CHECK(analyzeCoverage(checking, scan, ym(2024, 5), ym(2024, 4)).empty());

    // Opened mid-range and closed before the end: only those months (both included) are expected.
    checking[0].openedOn = makeDate(2023, 12, 20);
    checking[0].closedOn = makeDate(2024, 3, 2);
    coverage = analyzeCoverage(checking, scan, ym(2022, 1), ym(2025, 12));
    REQUIRE(coverage[0].years.size() == 4);
    CHECK(coverage[0].years[0].notOpen);   // 2022
    CHECK(coverage[0].years[1].found == months({12}));  // 2023: Nov was found but the account wasn't open yet
    CHECK(coverage[0].years[1].missing.empty());
    CHECK_FALSE(coverage[0].years[1].notOpen);
    CHECK(coverage[0].years[2].found == months({1, 3}));
    CHECK(coverage[0].years[2].missing == months({2}));
    CHECK(coverage[0].years[3].notOpen);   // 2025
}

TEST_CASE("The report reads as the Deficiency List")
{
    Coverage c;
    for (const char* name : {"2024.01.31", "2024.03.31"})
        c.dir.touch(std::string(name) + " Chase Checking 1234 (H).pdf");
    auto accounts = c.db.loadAccounts(c.caseId);
    accounts.erase(std::remove_if(accounts.begin(), accounts.end(), [&](const Account& a) { return a.id != c.checking; }),
                   accounts.end());
    accounts[0].openedOn = makeDate(2023, 6, 15);
    const auto coverage = analyzeCoverage(accounts, c.scan(), ym(2023, 1), ym(2024, 4));

    ReportSettings settings;
    settings.from = ym(2023, 1);
    settings.to = ym(2024, 4);
    auto report = buildDeficiencyReport("Smith & Jones", coverage, settings);
    CHECK(report.title == "Smith & Jones Deficiency List");
    CHECK(report.rangeLine == "January 2023 \xE2\x80\x93 April 2024");
    CHECK(report.showFound);
    CHECK(report.showMissing);
    REQUIRE(report.tables.size() == 1);
    CHECK(report.tables[0].heading == "Chase Checking 1234 (H)");
    CHECK(report.tables[0].note == "Opened June 15, 2023.");
    REQUIRE(report.tables[0].rows.size() == 2);
    CHECK(report.tables[0].rows[0].year == "2023");
    CHECK(report.tables[0].rows[0].found == "None");
    CHECK(report.tables[0].rows[0].missing == "Jun\xE2\x80\x93" "Dec");
    CHECK(report.tables[0].rows[1].year == "2024");
    CHECK(report.tables[0].rows[1].found == "Jan, Mar");
    CHECK(report.tables[0].rows[1].missing == "Feb, Apr");

    // "Through present" ends the range line with the word; the months are the caller's.
    settings.throughPresent = true;
    CHECK(buildDeficiencyReport("Smith", coverage, settings).rangeLine == "January 2023 \xE2\x80\x93 Present");

    // At least one column is always shown.
    settings.showFound = false;
    settings.showMissing = false;
    report = buildDeficiencyReport("Smith", coverage, settings);
    CHECK(report.showFound);
    CHECK_FALSE(report.showMissing);

    // Years the account wasn't open say so.
    accounts[0].openedOn = makeDate(2024, 2, 1);
    report = buildDeficiencyReport("Smith", analyzeCoverage(accounts, c.scan(), ym(2023, 1), ym(2024, 4)), settings);
    CHECK(report.tables[0].rows[0].year == "2023 (not open)");
    CHECK(report.tables[0].rows[0].found == "\xE2\x80\x94");

    CHECK(buildDeficiencyReport("", {}, settings).title == "Deficiency List");
}

TEST_CASE("Present ends with the last completed month")
{
    CHECK(lastCompletedMonth(makeDate(2026, 10, 6)) == ym(2026, 9));
    CHECK(lastCompletedMonth(makeDate(2026, 1, 1)) == ym(2025, 12));
}

namespace {

// An independent reader for the zip the writer produces: walks the central
// directory and checks every entry's checksum with its own CRC.
struct ZipEntry {
    std::string data;
    bool crcOk = false;
};

std::uint32_t refCrc(const std::string& s)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char ch : s) {
        crc ^= ch;
        for (int k = 0; k < 8; ++k) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

std::uint32_t rd(const std::string& b, std::size_t at, int bytes)
{
    std::uint32_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + i])) << (8 * i);
    return v;
}

std::map<std::string, ZipEntry> readZip(const std::string& zip)
{
    std::map<std::string, ZipEntry> out;
    REQUIRE(zip.size() > 22);
    const std::size_t eocd = zip.size() - 22;
    REQUIRE(rd(zip, eocd, 4) == 0x06054b50);
    const std::size_t count = rd(zip, eocd + 10, 2);
    std::size_t at = rd(zip, eocd + 16, 4);
    for (std::size_t i = 0; i < count; ++i) {
        REQUIRE(rd(zip, at, 4) == 0x02014b50);
        const std::uint32_t crc = rd(zip, at + 16, 4), size = rd(zip, at + 24, 4);
        const std::size_t nameLength = rd(zip, at + 28, 2), local = rd(zip, at + 42, 4);
        const std::string name = zip.substr(at + 46, nameLength);

        REQUIRE(rd(zip, local, 4) == 0x04034b50);
        const std::size_t dataAt = local + 30 + rd(zip, local + 26, 2) + rd(zip, local + 28, 2);
        ZipEntry entry;
        entry.data = zip.substr(dataAt, size);
        entry.crcOk = refCrc(entry.data) == crc && rd(zip, local + 14, 4) == crc;
        out[name] = entry;
        at += 46 + nameLength;
    }
    return out;
}

}  // namespace

TEST_CASE("The Word document is a valid package with the report in it")
{
    DeficiencyReport report;
    report.title = "Smith & Jones <Deficiency> List";
    report.rangeLine = "January 2023 \xE2\x80\x93 Present";
    report.tables.push_back({"Chase Checking 1234 (H)", "Opened June 15, 2023.",
                             {{"2023", "None", "Jun\xE2\x80\x93" "Dec"}, {"2024", "Jan, Mar", "Feb"}}});
    report.tables.push_back({"Fidelity IRA 5678 (W)", "", {{"2023", "Jan\xE2\x80\x93" "Dec", "None"}}});

    const auto zip = readZip(docxBytes(report));
    for (const char* name : {"[Content_Types].xml", "_rels/.rels", "word/document.xml",
                             "word/_rels/document.xml.rels", "word/styles.xml", "docProps/core.xml"}) {
        INFO(name);
        REQUIRE(zip.count(name) == 1);
        CHECK(zip.at(name).crcOk);
    }
    const std::string& doc = zip.at("word/document.xml").data;
    CHECK_THAT(doc, ContainsSubstring("Smith &amp; Jones &lt;Deficiency&gt; List"));
    CHECK_THAT(doc, ContainsSubstring("January 2023 \xE2\x80\x93 Present"));
    CHECK_THAT(doc, ContainsSubstring("Chase Checking 1234 (H)"));
    CHECK_THAT(doc, ContainsSubstring("Opened June 15, 2023."));
    CHECK_THAT(doc, ContainsSubstring("Months Found"));
    CHECK_THAT(doc, ContainsSubstring("Months Missing"));
    CHECK_THAT(doc, ContainsSubstring("Jun\xE2\x80\x93" "Dec"));
    CHECK(doc.find("<w:tbl>") != std::string::npos);

    // One table per account.
    std::size_t tables = 0;
    for (auto at = doc.find("<w:tbl>"); at != std::string::npos; at = doc.find("<w:tbl>", at + 1)) ++tables;
    CHECK(tables == 2);

    // Only the missing column.
    report.showFound = false;
    const auto missingOnly = readZip(docxBytes(report));
    CHECK_THAT(missingOnly.at("word/document.xml").data, ContainsSubstring("Months Missing"));
    CHECK(missingOnly.at("word/document.xml").data.find("Months Found") == std::string::npos);
}

TEST_CASE("Saving the Word document writes the file")
{
    testing::TempDir dir;
    DeficiencyReport report;
    report.title = "Smith Deficiency List";
    report.rangeLine = "January 2023 \xE2\x80\x93 Present";
    const fs::path file = dir.path() / "Smith Deficiency List.docx";
    writeDocx(file, report);
    CHECK(testing::readFile(file) == docxBytes(report));
    CHECK_THROWS(writeDocx(dir.path() / "no such folder" / "x.docx", report));
}

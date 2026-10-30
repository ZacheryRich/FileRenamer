#include <catch2/catch_test_macros.hpp>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "TestHelpers.h"
#include "finrenamer/PdfSplit.h"

using namespace finrenamer;
using testing::TempDir;
namespace fs = std::filesystem;

// All PDFs here are made up and built on the fly: page N says "Page N". The page size and
// font live on the Pages node (inherited), as many real PDFs do.
namespace {

fs::path makePdf(const TempDir& dir, const std::string& name, int pages, const std::string& user = "")
{
    QPDF pdf;
    pdf.emptyPDF();
    auto font = pdf.makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Font /Subtype /Type1 /Name /F1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"));
    for (int n = 1; n <= pages; ++n) {
        auto contents = QPDFObjectHandle::newStream(&pdf, "BT /F1 24 Tf 72 720 Td (Page " + std::to_string(n) + ") Tj ET\n");
        auto page = pdf.makeIndirectObject(QPDFObjectHandle::parse(
            &pdf, "<< /Type /Page /Contents " + contents.unparse() + " /Resources << /Font << /F1 " + font.unparse() + " >> >> >>"));
        pdf.addPage(page, false);
    }
    pdf.getRoot().getKey("/Pages").replaceKey("/MediaBox", QPDFObjectHandle::parse("[0 0 300 400]"));  // inherited

    const fs::path path = dir.path() / pathFromUtf8(name);
    QPDFWriter w(pdf, utf8FromPath(path).c_str());
    if (!user.empty()) w.setR6EncryptionParameters(user.c_str(), "owner-secret", false, false, false, false, false, false, qpdf_r3p_none, true);
    w.write();
    return path;
}

// The "Page N" texts of a PDF, in order, plus the size of its first page.
struct Content {
    std::vector<std::string> texts;
    std::string mediaBox;
};

Content contentOf(const fs::path& file)
{
    Content c;
    QPDF q;
    q.processFile(utf8FromPath(file).c_str());
    for (auto& page : QPDFPageDocumentHelper(q).getAllPages()) {
        auto data = page.getObjectHandle().getKey("/Contents").getStreamData(qpdf_dl_all);
        const std::string text(reinterpret_cast<const char*>(data->getBuffer()), data->getSize());
        const auto open = text.find('('), close = text.find(')');
        c.texts.push_back(text.substr(open + 1, close - open - 1));
    }
    c.mediaBox = QPDFPageDocumentHelper(q).getAllPages().at(0).getMediaBox().getArrayAsRectangle().urx == 300 && QPDFPageDocumentHelper(q).getAllPages().at(0).getMediaBox().getArrayAsRectangle().ury == 400 ? "300x400" : "other";
    return c;
}

}  // namespace

TEST_CASE("Pages marked as starts become ranges")
{
    using R = PageRange;
    CHECK(rangesFromStarts(10, {}) == std::vector<R>{{1, 10}});
    CHECK(rangesFromStarts(10, {1}) == std::vector<R>{{1, 10}});
    CHECK(rangesFromStarts(10, {4, 8}) == std::vector<R>{{1, 3}, {4, 7}, {8, 10}});  // page 1 is implied
    CHECK(rangesFromStarts(10, {1, 10}) == std::vector<R>{{1, 9}, {10, 10}});
    CHECK(rangesFromStarts(5, {0, -2, 6, 99, 3}) == std::vector<R>{{1, 2}, {3, 5}});  // out of range ignored
    CHECK(rangesFromStarts(0, {1}).empty());
    CHECK(rangeText({3, 3}) == "3");
    CHECK(rangeText({3, 5}) == "3-5");
}

TEST_CASE("Every N pages")
{
    CHECK(startsEvery(12, 4) == std::set<int>{1, 5, 9});
    CHECK(startsEvery(12, 5) == std::set<int>{1, 6, 11});
    CHECK(startsEvery(3, 10) == std::set<int>{1});
    CHECK(startsEvery(12, 0) == std::set<int>{1});
}

TEST_CASE("Default part names sort in order")
{
    CHECK(defaultPartName("Chase 2025", 1, 12) == "Chase 2025 Part 01");
    CHECK(defaultPartName("Chase 2025", 12, 12) == "Chase 2025 Part 12");
    CHECK(defaultPartName("Big", 7, 3) == "Big Part 07");
    CHECK(defaultPartName("Big", 7, 120) == "Big Part 007");
}

TEST_CASE("A PDF is split at the marked pages")
{
    TempDir dir;
    const auto pdf = makePdf(dir, "year.pdf", 10);
    const auto original = testing::readFile(pdf);

    const auto info = readPdfPages(pdf);
    REQUIRE(info.ok);
    CHECK(info.pageCount == 10);

    const auto ranges = rangesFromStarts(10, {4, 8});
    std::vector<SplitPart> parts;
    for (std::size_t i = 0; i < ranges.size(); ++i) parts.push_back({ranges[i], defaultPartName("year", int(i) + 1, int(ranges.size()))});

    const auto out = dir.path() / "out";
    const auto results = splitPdf(pdf, parts, out);
    REQUIRE(results.size() == 3);
    for (const auto& r : results) REQUIRE(r.ok);

    CHECK(results[0].file == out / "year Part 01.pdf");
    CHECK(contentOf(results[0].file).texts == std::vector<std::string>{"Page 1", "Page 2", "Page 3"});
    CHECK(contentOf(results[1].file).texts == std::vector<std::string>{"Page 4", "Page 5", "Page 6", "Page 7"});
    CHECK(contentOf(results[2].file).texts == std::vector<std::string>{"Page 8", "Page 9", "Page 10"});
    CHECK(contentOf(results[1].file).mediaBox == "300x400");  // the inherited page size travelled
    CHECK(testing::readFile(pdf) == original);                         // the original is untouched
    for (const auto& e : fs::directory_iterator(out)) CHECK(e.path().extension() == ".pdf");  // no temp files left
}

TEST_CASE("Nothing is overwritten; names are cleaned")
{
    TempDir dir;
    const auto pdf = makePdf(dir, "year.pdf", 4);
    const auto out = dir.path() / "out";
    fs::create_directories(out);
    const auto existing = dir.touch("out/March.pdf", "keep me");

    const auto results = splitPdf(pdf, {{{1, 2}, "March"}, {{3, 4}, "March"}, {{3, 3}, "a/b:c?"}}, out);
    REQUIRE(results.size() == 3);
    for (const auto& r : results) REQUIRE(r.ok);
    CHECK(testing::readFile(existing) == "keep me");
    CHECK(results[0].file == out / "March (2).pdf");
    CHECK(results[1].file == out / "March (3).pdf");
    CHECK(results[2].file == out / "a-b-c.pdf");
}

TEST_CASE("Passwords, damaged files and bad ranges")
{
    TempDir dir;
    const auto secret = makePdf(dir, "secret.pdf", 3, "open-me");
    const auto out = dir.path() / "out";

    CHECK(readPdfPages(secret).needsPassword);
    CHECK_FALSE(readPdfPages(secret).wrongPassword);
    CHECK(readPdfPages(secret, "nope").wrongPassword);
    CHECK(readPdfPages(secret, "open-me").pageCount == 3);

    CHECK_FALSE(splitPdf(secret, {{{1, 3}, "x"}}, out)[0].ok);
    REQUIRE(splitPdf(secret, {{{2, 3}, "x"}}, out, "open-me")[0].ok);
    CHECK(readPdfPages(out / "x.pdf").ok);  // the pieces need no password
    CHECK(readPdfPages(out / "x.pdf").pageCount == 2);

    const auto junk = dir.touch("junk.pdf", "not a pdf");
    CHECK_FALSE(readPdfPages(junk).ok);
    CHECK_FALSE(readPdfPages(junk).message.empty());
    CHECK_FALSE(splitPdf(junk, {{{1, 1}, "x"}}, out)[0].ok);

    const auto plain = makePdf(dir, "plain.pdf", 2);
    const auto bad = splitPdf(plain, {{{1, 5}, "too far"}, {{0, 1}, "zero"}, {{2, 1}, "backwards"}, {{1, 2}, "fine"}}, out);
    CHECK_FALSE(bad[0].ok);
    CHECK_FALSE(bad[1].ok);
    CHECK_FALSE(bad[2].ok);
    CHECK(bad[3].ok);  // one bad range doesn't stop the others
    CHECK_FALSE(fs::exists(out / "too far.pdf"));
}

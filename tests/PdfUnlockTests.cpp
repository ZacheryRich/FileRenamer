#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "TestHelpers.h"
#include "finrenamer/PdfUnlock.h"

using namespace finrenamer;
using testing::TempDir;
namespace fs = std::filesystem;

// All PDFs here are made up and built on the fly.

namespace {

enum class Enc { None, R2, R3, R4Aes, R6 };

// A one-page PDF saying "Hello World", encrypted as asked: restrictions on printing, copying and editing.
fs::path makePdf(const TempDir& dir, const std::string& name, Enc enc, const std::string& user = "", const std::string& owner = "")
{
    QPDF pdf;
    pdf.emptyPDF();
    auto font = pdf.makeIndirectObject(
        QPDFObjectHandle::parse("<< /Type /Font /Subtype /Type1 /Name /F1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"));
    auto contents = QPDFObjectHandle::newStream(&pdf, "BT /F1 24 Tf 72 720 Td (Hello World) Tj ET\n");
    auto page = pdf.makeIndirectObject(QPDFObjectHandle::parse(
        &pdf, "<< /Type /Page /MediaBox [0 0 612 792] /Contents " + contents.unparse() +
                  " /Resources << /Font << /F1 " + font.unparse() + " >> >> >>"));
    pdf.addPage(page, true);

    const fs::path path = dir.path() / pathFromUtf8(name);
    QPDFWriter w(pdf, utf8FromPath(path).c_str());
    const char* u = user.c_str();
    const char* o = owner.empty() ? "owner-secret" : owner.c_str();
    switch (enc) {
    case Enc::None: break;
    case Enc::R2: w.setR2EncryptionParametersInsecure(u, o, false, false, false, false); break;
    case Enc::R3: w.setR3EncryptionParametersInsecure(u, o, false, false, false, false, false, false, qpdf_r3p_none); break;
    case Enc::R4Aes: w.setR4EncryptionParametersInsecure(u, o, false, false, false, false, false, false, qpdf_r3p_none, true, true); break;
    case Enc::R6: w.setR6EncryptionParameters(u, o, false, false, false, false, false, false, qpdf_r3p_none, true); break;
    }
    w.write();
    return path;
}

// The text of page 1, or "" if it can't be opened.
std::string pageContent(const fs::path& file)
{
    try {
        QPDF q;
        q.processFile(utf8FromPath(file).c_str());
        const auto pages = QPDFPageDocumentHelper(q).getAllPages();
        if (pages.size() != 1) return {};
        auto contents = pages[0].getObjectHandle().getKey("/Contents");
        auto data = contents.getStreamData(qpdf_dl_all);
        return std::string(reinterpret_cast<const char*>(data->getBuffer()), data->getSize());
    } catch (...) {
        return {};
    }
}

bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}  // namespace

TEST_CASE("A PDF with no protection is left alone")
{
    TempDir dir;
    const auto pdf = makePdf(dir, "plain.pdf", Enc::None);
    CHECK(inspectPdf(pdf).lock == PdfLock::NotProtected);
    const auto out = dir.path() / "out.pdf";
    CHECK(unlockPdf(pdf, out).outcome == UnlockOutcome::NotProtected);
    CHECK_FALSE(fs::exists(out));
}

TEST_CASE("Restrictions are found and removed, whatever the encryption")
{
    const auto [enc, label] = GENERATE(std::pair{Enc::R2, "RC4 40-bit"}, std::pair{Enc::R3, "RC4 128-bit"},
                                       std::pair{Enc::R4Aes, "AES 128-bit"}, std::pair{Enc::R6, "AES 256-bit"});
    INFO(label);
    TempDir dir;
    const auto pdf = makePdf(dir, "locked.pdf", enc);
    const auto original = testing::readFile(pdf);

    const auto info = inspectPdf(pdf);  // opens with no password: only restrictions
    REQUIRE(info.lock == PdfLock::Restricted);
    CHECK(has(info.restrictions, "Printing"));
    CHECK(has(info.restrictions, "Copying text and images"));
    CHECK(has(info.restrictions, "Editing"));

    const auto out = dir.path() / "unlocked.pdf";
    const auto r = unlockPdf(pdf, out);
    REQUIRE(r.outcome == UnlockOutcome::Unlocked);
    CHECK(inspectPdf(out).lock == PdfLock::NotProtected);
    CHECK(pageContent(out).find("Hello World") != std::string::npos);  // content survives
    CHECK(testing::readFile(pdf) == original);                         // the original is untouched
    CHECK_FALSE(fs::exists(out.string() + ".unlocking"));
}

TEST_CASE("A PDF that needs a password to open")
{
    const auto enc = GENERATE(Enc::R3, Enc::R4Aes, Enc::R6);
    TempDir dir;
    const auto pdf = makePdf(dir, "secret.pdf", enc, "open-me", "owner-pw");
    const auto out = dir.path() / "out.pdf";

    CHECK(inspectPdf(pdf).lock == PdfLock::NeedsPassword);
    CHECK_FALSE(inspectPdf(pdf).wrongPassword);
    CHECK(unlockPdf(pdf, out).outcome == UnlockOutcome::NeedsPassword);
    CHECK(inspectPdf(pdf, "nope").wrongPassword);
    CHECK(unlockPdf(pdf, out, "nope").outcome == UnlockOutcome::WrongPassword);
    CHECK_FALSE(fs::exists(out));

    SECTION("the user password")
    {
        CHECK(inspectPdf(pdf, "open-me").lock == PdfLock::Restricted);
        REQUIRE(unlockPdf(pdf, out, "open-me").outcome == UnlockOutcome::Unlocked);
        CHECK(inspectPdf(out).lock == PdfLock::NotProtected);  // opens with no password now
        CHECK(pageContent(out).find("Hello World") != std::string::npos);
    }
    SECTION("the owner password")
    {
        REQUIRE(unlockPdf(pdf, out, "owner-pw").outcome == UnlockOutcome::Unlocked);
        CHECK(inspectPdf(out).lock == PdfLock::NotProtected);
    }
}

TEST_CASE("Files that aren't PDFs are reported, not crashed on")
{
    TempDir dir;
    const auto junk = dir.touch("junk.pdf", "this is not a pdf at all");
    CHECK(inspectPdf(junk).lock == PdfLock::Unreadable);
    CHECK_FALSE(inspectPdf(junk).message.empty());
    const auto r = unlockPdf(junk, dir.path() / "out.pdf");
    CHECK(r.outcome == UnlockOutcome::Failed);
    CHECK_FALSE(fs::exists(dir.path() / "out.pdf"));
    CHECK(inspectPdf(dir.path() / "missing.pdf").lock == PdfLock::Unreadable);
}

TEST_CASE("The original is never replaced and nothing is overwritten")
{
    TempDir dir;
    const auto pdf = makePdf(dir, "locked.pdf", Enc::R6);
    const auto original = testing::readFile(pdf);

    CHECK(unlockPdf(pdf, pdf).outcome == UnlockOutcome::Failed);
    CHECK(testing::readFile(pdf) == original);

    const auto existing = dir.touch("existing.pdf", "do not touch");
    CHECK(unlockPdf(pdf, existing).outcome == UnlockOutcome::Failed);
    CHECK(testing::readFile(existing) == "do not touch");
}

TEST_CASE("Names with accents work, and copies get a free name")
{
    TempDir dir;
    const auto pdf = makePdf(dir, "Jos\xC3\xA9 statement.pdf", Enc::R6);
    const auto folder = dir.path() / "out";
    fs::create_directories(folder);

    const auto first = unlockedCopyPath(folder, pdf);
    CHECK(utf8FromPath(first.filename()) == "Jos\xC3\xA9 statement.pdf");
    REQUIRE(unlockPdf(pdf, first).outcome == UnlockOutcome::Unlocked);
    CHECK(fs::exists(first));

    const auto second = unlockedCopyPath(folder, pdf);
    CHECK(utf8FromPath(second.filename()) == "Jos\xC3\xA9 statement (2).pdf");
    REQUIRE(unlockPdf(pdf, second).outcome == UnlockOutcome::Unlocked);
    CHECK(utf8FromPath(unlockedCopyPath(folder, pdf).filename()) == "Jos\xC3\xA9 statement (3).pdf");
}

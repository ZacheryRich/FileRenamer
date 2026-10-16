#include "finrenamer/PdfUnlock.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>

#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {

namespace {

// Opens `pdf` in `q`. Returns Unreadable's message in `error`, or sets `needsPassword`.
enum class OpenResult { Ok, NeedsPassword, Unreadable };

OpenResult openPdf(QPDF& q, const fs::path& pdf, const std::string& password, std::string& error)
{
    q.setSuppressWarnings(true);
    q.setMaxWarnings(1000);
    try {
        q.processFile(utf8FromPath(pdf).c_str(), password.empty() ? nullptr : password.c_str());
        return OpenResult::Ok;
    } catch (const QPDFExc& e) {
        if (e.getErrorCode() == qpdf_e_password) return OpenResult::NeedsPassword;
        error = "This doesn't look like a readable PDF (" + std::string(e.getMessageDetail()) + ").";
    } catch (const std::exception& e) {
        error = "This PDF couldn't be read: " + std::string(e.what());
    }
    return OpenResult::Unreadable;
}

std::vector<std::string> restrictionsOf(QPDF& q)
{
    std::vector<std::string> r;
    if (!q.allowPrintLowRes() || !q.allowPrintHighRes()) r.push_back("Printing");
    if (!q.allowExtractAll()) r.push_back("Copying text and images");
    if (!q.allowModifyOther() || !q.allowModifyAll()) r.push_back("Editing");
    if (!q.allowModifyAnnotation()) r.push_back("Commenting");
    if (!q.allowModifyForm()) r.push_back("Filling in forms");
    if (!q.allowModifyAssembly()) r.push_back("Rearranging pages");
    return r;
}

}  // namespace

PdfInspection inspectPdf(const fs::path& pdf, const std::string& password)
{
    PdfInspection result;
    QPDF q;
    std::string error;
    switch (openPdf(q, pdf, password, error)) {
    case OpenResult::NeedsPassword:
        result.lock = PdfLock::NeedsPassword;
        result.wrongPassword = !password.empty();
        return result;
    case OpenResult::Unreadable:
        result.lock = PdfLock::Unreadable;
        result.message = error;
        return result;
    case OpenResult::Ok: break;
    }
    if (!q.isEncrypted()) {
        result.lock = PdfLock::NotProtected;
        return result;
    }
    result.lock = PdfLock::Restricted;
    result.restrictions = restrictionsOf(q);
    return result;
}

UnlockResult unlockPdf(const fs::path& source, const fs::path& destination, const std::string& password)
{
    UnlockResult result;
    std::error_code ec;
    if (caseFoldKey(fs::absolute(source, ec)) == caseFoldKey(fs::absolute(destination, ec))) {
        result.message = "The copy can't be saved over the original.";
        return result;
    }
    if (fs::exists(destination, ec)) {
        result.message = "A file with that name already exists, so it was left alone.";
        return result;
    }

    QPDF q;
    std::string error;
    switch (openPdf(q, source, password, error)) {
    case OpenResult::NeedsPassword:
        result.outcome = password.empty() ? UnlockOutcome::NeedsPassword : UnlockOutcome::WrongPassword;
        result.message = password.empty() ? "Needs a password to open." : "That password doesn't open this PDF.";
        return result;
    case OpenResult::Unreadable:
        result.message = error;
        return result;
    case OpenResult::Ok: break;
    }
    if (!q.isEncrypted()) {
        result.outcome = UnlockOutcome::NotProtected;
        result.message = "Not protected, so there was nothing to remove.";
        return result;
    }
    result.repaired = q.anyWarnings();

    const fs::path temp = fs::path(destination).concat(".unlocking");
    try {
        QPDFWriter writer(q, utf8FromPath(temp).c_str());
        writer.setPreserveEncryption(false);  // the whole point: write it unprotected
        writer.write();
    } catch (const std::exception& e) {
        std::error_code ignore;
        fs::remove(temp, ignore);
        result.message = "The unlocked copy couldn't be written: " + std::string(e.what());
        return result;
    }
    fs::rename(temp, destination, ec);
    if (ec) {
        std::error_code ignore;
        fs::remove(temp, ignore);
        result.message = "The unlocked copy couldn't be saved: " + ec.message();
        return result;
    }
    result.outcome = UnlockOutcome::Unlocked;
    result.repaired = result.repaired || q.anyWarnings();
    return result;
}

fs::path unlockedCopyPath(const fs::path& folder, const fs::path& source)
{
    const fs::path name = source.filename();
    fs::path candidate = folder / name;
    std::error_code ec;
    for (int n = 2; fs::exists(candidate, ec) && n < 10000; ++n) {
        const std::string stem = utf8FromPath(name.stem());
        const std::string ext = utf8FromPath(name.extension());
        candidate = folder / pathFromUtf8(stem + " (" + std::to_string(n) + ")" + ext);
    }
    return candidate;
}

}  // namespace finrenamer

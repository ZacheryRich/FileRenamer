#include "finrenamer/PdfSplit.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>

#include "finrenamer/FilenameBuilder.h"
#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {

namespace {

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

}  // namespace

PdfPageInfo readPdfPages(const fs::path& pdf, const std::string& password)
{
    PdfPageInfo info;
    QPDF q;
    switch (openPdf(q, pdf, password, info.message)) {
    case OpenResult::NeedsPassword:
        info.needsPassword = true;
        info.wrongPassword = !password.empty();
        info.message = info.wrongPassword ? "That password doesn't open this PDF." : "Needs a password to open.";
        return info;
    case OpenResult::Unreadable: return info;
    case OpenResult::Ok: break;
    }
    try {
        info.pageCount = static_cast<int>(QPDFPageDocumentHelper(q).getAllPages().size());
    } catch (const std::exception& e) {
        info.message = "The pages of this PDF couldn't be read: " + std::string(e.what());
        return info;
    }
    if (info.pageCount < 1) {
        info.message = "This PDF has no pages.";
        return info;
    }
    info.ok = true;
    return info;
}

std::vector<PageRange> rangesFromStarts(int pageCount, const std::set<int>& startPages)
{
    std::vector<PageRange> ranges;
    if (pageCount < 1) return ranges;
    std::set<int> starts{1};
    for (int p : startPages)
        if (p >= 1 && p <= pageCount) starts.insert(p);
    for (auto it = starts.begin(); it != starts.end(); ++it) {
        const auto next = std::next(it);
        ranges.push_back({*it, next == starts.end() ? pageCount : *next - 1});
    }
    return ranges;
}

std::set<int> startsEvery(int pageCount, int step)
{
    std::set<int> starts{1};
    if (step < 1) return starts;
    for (int p = 1 + step; p <= pageCount; p += step) starts.insert(p);
    return starts;
}

std::string rangeText(const PageRange& range)
{
    if (range.first == range.last) return std::to_string(range.first);
    return std::to_string(range.first) + "-" + std::to_string(range.last);
}

std::string defaultPartName(const std::string& sourceStem, int index, int total)
{
    const int width = std::max<int>(2, static_cast<int>(std::to_string(std::max(total, 1)).size()));
    std::string number = std::to_string(index);
    if (static_cast<int>(number.size()) < width) number.insert(0, width - number.size(), '0');
    return sourceStem + " Part " + number;
}

fs::path splitFilePath(const fs::path& folder, const std::string& name)
{
    std::string clean = sanitizeComponent(name);
    if (clean.empty()) clean = "Part";
    fs::path candidate = folder / pathFromUtf8(clean + ".pdf");
    std::error_code ec;
    for (int n = 2; fs::exists(candidate, ec) && n < 10000; ++n)
        candidate = folder / pathFromUtf8(clean + " (" + std::to_string(n) + ").pdf");
    return candidate;
}

std::vector<SplitPartResult> splitPdf(const fs::path& source, const std::vector<SplitPart>& parts, const fs::path& folder,
                                      const std::string& password)
{
    std::vector<SplitPartResult> results(parts.size());
    auto failAll = [&](const std::string& message) {
        for (auto& r : results) r.message = message;
        return results;
    };

    QPDF q;
    std::string error;
    switch (openPdf(q, source, password, error)) {
    case OpenResult::NeedsPassword:
        return failAll(password.empty() ? "Needs a password to open." : "That password doesn't open this PDF.");
    case OpenResult::Unreadable: return failAll(error);
    case OpenResult::Ok: break;
    }

    std::vector<QPDFPageObjectHelper> pages;
    try {
        q.pushInheritedAttributesToPage();  // sizes and resources a page inherits travel with it
        pages = QPDFPageDocumentHelper(q).getAllPages();
    } catch (const std::exception& e) {
        return failAll("The pages of this PDF couldn't be read: " + std::string(e.what()));
    }

    std::error_code ec;
    fs::create_directories(folder, ec);
    if (!fs::is_directory(folder, ec)) return failAll("The output folder couldn't be created.");

    for (std::size_t i = 0; i < parts.size(); ++i) {
        const PageRange range = parts[i].pages;
        SplitPartResult& result = results[i];
        if (range.first < 1 || range.last < range.first || range.last > static_cast<int>(pages.size())) {
            result.message = "Those pages aren't in the PDF.";
            continue;
        }
        const fs::path destination = splitFilePath(folder, parts[i].name);
        const fs::path temp = fs::path(destination).concat(".splitting");
        try {
            QPDF out;
            out.emptyPDF();
            QPDFPageDocumentHelper outPages(out);
            for (int p = range.first; p <= range.last; ++p) outPages.addPage(pages[static_cast<std::size_t>(p - 1)], false);
            QPDFWriter writer(out, utf8FromPath(temp).c_str());
            writer.write();
        } catch (const std::exception& e) {
            fs::remove(temp, ec);
            result.message = "The file couldn't be written: " + std::string(e.what());
            continue;
        }
        if (fs::exists(destination, ec)) {  // appeared since we looked: never overwrite
            fs::remove(temp, ec);
            result.message = "A file with that name appeared, so it was left alone.";
            continue;
        }
        fs::rename(temp, destination, ec);
        if (ec) {
            std::error_code ignore;
            fs::remove(temp, ignore);
            result.message = "The file couldn't be saved: " + ec.message();
            continue;
        }
        result.ok = true;
        result.file = destination;
    }
    return results;
}

}  // namespace finrenamer

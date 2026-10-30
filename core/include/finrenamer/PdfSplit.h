#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace finrenamer {

// PDF Splitter: cuts one PDF into several files at pages the user marks (for example a year of
// monthly statements saved as one big PDF). The original is never changed and nothing is
// overwritten. Page numbers are 1-based.

struct PdfPageInfo {
    bool ok = false;
    int pageCount = 0;
    bool needsPassword = false;  // can't be opened without one (or the one given was wrong)
    bool wrongPassword = false;  // a password was given and it didn't work
    std::string message;         // for the user when !ok and !needsPassword
};

// Opens the PDF just far enough to count its pages. `password` is tried when not empty.
PdfPageInfo readPdfPages(const std::filesystem::path& pdf, const std::string& password = {});

struct PageRange {
    int first = 1;  // inclusive
    int last = 1;   // inclusive
    int count() const { return last - first + 1; }
    bool operator==(const PageRange&) const = default;
};

// The files a split makes: `startPages` are the pages that begin a new file. Page 1 always
// begins the first file (whether or not it is listed), and pages outside 1..pageCount are ignored.
// pageCount < 1 gives no ranges.
std::vector<PageRange> rangesFromStarts(int pageCount, const std::set<int>& startPages);

// Every `step` pages: {1, 1+step, 1+2*step, ...} up to pageCount. step < 1 gives just {1}.
std::set<int> startsEvery(int pageCount, int step);

// "3" for one page, "3-5" for several.
std::string rangeText(const PageRange& range);

// "<source name> Part 01" ... : numbers are padded to the width of `total` (at least 2) so the
// files sort in order in Explorer.
std::string defaultPartName(const std::string& sourceStem, int index, int total);

struct SplitPart {
    PageRange pages;
    std::string name;  // file name without ".pdf"; cleaned for Windows before use
};

struct SplitPartResult {
    bool ok = false;
    std::filesystem::path file;  // where it was written when ok
    std::string message;         // for the user when !ok
};

// A free file name in `folder` for `name` + ".pdf": "name.pdf", else "name (2).pdf", ...
std::filesystem::path splitFilePath(const std::filesystem::path& folder, const std::string& name);

// Writes one PDF per part into `folder` (created if missing). Each is written through a
// temporary file, so a failure leaves nothing half-written; one part failing doesn't stop the
// others. The files are written without a password or restrictions. Everything else a page
// needs (fonts, images) is copied with it; document-level extras (bookmarks, form fields that
// span pages) are not carried over.
std::vector<SplitPartResult> splitPdf(const std::filesystem::path& source, const std::vector<SplitPart>& parts,
                                      const std::filesystem::path& folder, const std::string& password = {});

}  // namespace finrenamer

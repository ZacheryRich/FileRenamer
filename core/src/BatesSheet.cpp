#include "finrenamer/BatesSheet.h"

#include <OpenXLSX.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {

namespace {

const char* kHeaderA = "Name";
const char* kHeaderB = "Begin Bates";
const char* kHeaderC = "End Bates";

struct Styles {
    OpenXLSX::XLStyleIndex header = 0;
    OpenXLSX::XLStyleIndex bold = 0;
};

// Two cell formats (bold; bold on light grey) based on the workbook's first font.
Styles makeStyles(OpenXLSX::XLDocument& doc)
{
    using namespace OpenXLSX;
    Styles s;
    auto& styles = doc.styles();

    const XLStyleIndex boldFont = styles.fonts().create(styles.fonts()[0]);
    styles.fonts()[boldFont].setBold(true);

    XLStyleIndex fill = 0;
    {
        fill = styles.fills().create(styles.fills()[0]);
        auto f = styles.fills()[fill];
        f.setFillType(XLPatternFill);
        f.setPatternType(XLPatternSolid);
        f.setColor(XLColor(0xE6, 0xE6, 0xE6));
        f.setBackgroundColor(XLColor(0xE6, 0xE6, 0xE6));
    }

    s.bold = styles.cellFormats().create(styles.cellFormats()[0]);
    {
        auto cf = styles.cellFormats()[s.bold];
        cf.setFontIndex(boldFont);
        cf.setApplyFont(true);
    }
    s.header = styles.cellFormats().create(styles.cellFormats()[0]);
    {
        auto cf = styles.cellFormats()[s.header];
        cf.setFontIndex(boldFont);
        cf.setFillIndex(fill);
        cf.setApplyFont(true);
        cf.setApplyFill(true);
    }
    return s;
}

std::string textOf(const OpenXLSX::XLCell& cell)
{
    using OpenXLSX::XLValueType;
    const auto& v = cell.value();
    switch (v.type()) {
    case XLValueType::String: return v.get<std::string>();
    case XLValueType::Integer: return std::to_string(v.get<std::int64_t>());
    case XLValueType::Float: {
        std::string t = std::to_string(v.get<double>());
        t.erase(t.find_last_not_of('0') + 1);
        if (!t.empty() && t.back() == '.') t.pop_back();
        return t;
    }
    default: return {};
    }
}

std::string cellText(const OpenXLSX::XLWorksheet& ws, uint32_t row, uint16_t col)
{
    // findCell reads without creating the cell (cell() would add empty ones).
    const auto cell = ws.findCell(row, col);
    if (!cell) return {};
    return textOf(cell);
}

// Last row that has anything in columns A-C (0 if the sheet is empty).
uint32_t lastUsedRow(const OpenXLSX::XLWorksheet& ws)
{
    for (uint32_t r = ws.rowCount(); r >= 1; --r) {
        for (uint16_t c = 1; c <= 3; ++c)
            if (!cellText(ws, r, c).empty()) return r;
    }
    return 0;
}

void openOrThrow(OpenXLSX::XLDocument& doc, const fs::path& file)
{
    try {
        doc.open(utf8FromPath(file));
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "Couldn't open the spreadsheet. If it is open in Excel, close it and try again.\n(" + std::string(e.what()) + ")");
    }
}

void saveOrThrow(OpenXLSX::XLDocument& doc)
{
    try {
        doc.save();
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "Couldn't save the spreadsheet. If it is open in Excel, close it and try again.\n(" + std::string(e.what()) + ")");
    }
    doc.close();
}

void writeHeaders(OpenXLSX::XLWorksheet& ws, const Styles& st, bool batesColumns = true)
{
    const char* names[3] = {kHeaderA, kHeaderB, kHeaderC};
    for (uint16_t c = 1; c <= (batesColumns ? 3 : 1); ++c) {
        auto cell = ws.cell(1, c);
        if (cellText(ws, 1, c).empty()) cell.value() = names[c - 1];
        cell.setCellFormat(st.header);
    }
}

}  // namespace

ListingResult writeListing(const fs::path& workbook, const std::vector<FolderListing>& listing, bool batesColumns)
{
    using namespace OpenXLSX;
    ListingResult result;
    XLDocument doc;

    std::error_code ec;
    result.created = !fs::exists(workbook, ec);
    if (result.created) {
        try {
            doc.create(utf8FromPath(workbook), XLForceOverwrite);
        } catch (const std::exception& e) {
            throw std::runtime_error("Couldn't create the spreadsheet: " + std::string(e.what()));
        }
    } else {
        openOrThrow(doc, workbook);
    }

    auto ws = doc.workbook().worksheet(1);
    // A new workbook gets the same font as the sample sheet (Excel's current default).
    if (result.created) {
        auto font = doc.styles().fonts()[0];
        font.setFontName("Aptos Narrow");
        font.setFontSize(11);
    }
    const Styles st = makeStyles(doc);

    uint32_t last = lastUsedRow(ws);
    if (last == 0) result.created = true;  // an empty sheet is treated like a new one
    uint32_t row = 0;
    if (last == 0) {
        writeHeaders(ws, st, batesColumns);
        row = 3;  // row 2 stays blank
    } else {
        // Make sure the header (and, if wanted, the two Bates headers) are there, then one blank row.
        if (cellText(ws, 1, 1).empty() && cellText(ws, 1, 2).empty() && cellText(ws, 1, 3).empty()) {
            // no header at all: leave the user's layout alone
        } else {
            writeHeaders(ws, st, batesColumns);
        }
        row = last + 2;
    }
    result.firstRow = row;

    std::size_t longest = 4;
    for (std::size_t i = 0; i < listing.size(); ++i) {
        if (i > 0) ++row;  // blank row between folders
        const FolderListing& folder = listing[i];
        auto head = ws.cell(row, 1);
        head.value() = folder.folder;
        head.setCellFormat(st.bold);
        longest = std::max(longest, folder.folder.size());
        ++row;
        ++result.folders;
        for (const std::string& name : folder.files) {
            ws.cell(row, 1).value() = name;
            longest = std::max(longest, name.size());
            ++row;
            ++result.files;
        }
    }
    result.lastRow = listing.empty() ? 0 : row - 1;

    // Widen column A to fit (never narrower than it already is).
    auto colA = ws.column(1);
    const float wanted = static_cast<float>(std::min<std::size_t>(longest + 2, 100));
    if (result.created || colA.width() < wanted) colA.setWidth(std::max(wanted, 28.7f));
    if (result.created) {
        ws.column(2).setWidth(14.0f);
        ws.column(3).setWidth(14.0f);
    }

    saveOrThrow(doc);
    return result;
}

BatesApplyResult applyBatesLog(const fs::path& workbook, const BatesLog& log)
{
    using namespace OpenXLSX;
    BatesApplyResult result;
    XLDocument doc;
    openOrThrow(doc, workbook);

    auto ws = doc.workbook().worksheet(1);
    const Styles st = makeStyles(doc);

    std::set<std::string> used;
    std::map<std::string, int> seen;  // key -> rows using it
    const uint32_t rows = lastUsedRow(ws);
    for (uint32_t r = 1; r <= rows; ++r) {
        const std::string name = cellText(ws, r, 1);
        if (name.empty() || name[0] == '/') continue;     // blank row or folder row
        if (r == 1 && name == kHeaderA) continue;          // header
        ++result.fileRows;

        const std::string key = matchKey(name);
        const auto it = log.entries.find(key);
        if (it == log.entries.end()) {
            result.unmatchedRows.push_back(r);
            result.unmatchedNames.push_back(name);
            continue;
        }
        ws.cell(r, 2).value() = it->second.begin;
        ws.cell(r, 3).value() = it->second.end;
        ++result.matched;
        used.insert(key);
        if (++seen[key] == 2) result.sharedNames.push_back(name);
    }

    for (const auto& [key, display] : log.displayNames)
        if (!used.count(key)) result.unusedLogNames.push_back(display);
    std::sort(result.unusedLogNames.begin(), result.unusedLogNames.end());

    writeHeaders(ws, st);
    saveOrThrow(doc);
    return result;
}

}  // namespace finrenamer

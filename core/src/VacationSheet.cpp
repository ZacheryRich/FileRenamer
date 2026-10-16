#include "finrenamer/VacationSheet.h"

#include <OpenXLSX.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <pugixml.hpp>
#include <sstream>

#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {

namespace {

// ---- small text helpers -------------------------------------------------------

std::string lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimmed(const std::string& s)
{
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::string fixNewlines(std::string s)
{
    // Excel writes "_x000D_" for a carriage return it keeps; line breaks are "\n".
    for (std::size_t p; (p = s.find("_x000D_")) != std::string::npos;) s.erase(p, 7);
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            if (i + 1 < s.size() && s[i + 1] == '\n') continue;  // "\r\n" -> "\n"
            out += '\n';
        } else {
            out += s[i];
        }
    }
    return out;
}

// XML 1.0 can't hold most control characters.
std::string xmlSafe(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 && c != '\t' && c != '\n') continue;
        out += c;
    }
    return out;
}

std::size_t columnIndex(const std::string& ref)  // "B7" -> 1
{
    std::size_t n = 0;
    for (char c : ref) {
        if (!std::isalpha(static_cast<unsigned char>(c))) break;
        n = n * 26 + static_cast<std::size_t>(std::toupper(static_cast<unsigned char>(c)) - 'A' + 1);
    }
    return n == 0 ? 0 : n - 1;
}

std::size_t rowNumber(const std::string& ref)  // "B7" -> 7
{
    std::size_t i = 0;
    while (i < ref.size() && std::isalpha(static_cast<unsigned char>(ref[i]))) ++i;
    return static_cast<std::size_t>(std::strtoull(ref.c_str() + i, nullptr, 10));
}

std::string columnLetters(std::size_t index)  // 1 -> "B"
{
    std::string s;
    for (std::size_t n = index + 1; n > 0; n = (n - 1) / 26) s.insert(s.begin(), static_cast<char>('A' + (n - 1) % 26));
    return s;
}

// ---- the package ---------------------------------------------------------------

void parseXml(const std::string& data, pugi::xml_document& doc, const char* what)
{
    const auto r = doc.load_buffer(data.data(), data.size(),
                                   pugi::parse_default | pugi::parse_declaration | pugi::parse_ws_pcdata_single,
                                   pugi::encoding_utf8);
    if (!r) throw std::runtime_error(std::string("The spreadsheet is damaged (") + what + ": " + r.description() + ").");
}

std::string xmlText(const pugi::xml_document& doc)
{
    std::ostringstream out;
    doc.save(out, "", pugi::format_raw, pugi::encoding_utf8);
    return out.str();
}

std::string entry(const OpenXLSX::XLZipArchive& zip, const std::string& name)
{
    if (!zip.hasEntry(name)) throw std::runtime_error("The spreadsheet is damaged (missing " + name + ").");
    return zip.getEntry(name);
}

// Opens the .xlsx for reading; a clear message when it isn't one.
void openZip(OpenXLSX::XLZipArchive& zip, const fs::path& workbook)
{
    std::error_code ec;
    if (!fs::exists(workbook, ec)) throw std::runtime_error("The spreadsheet was not found:\n" + utf8FromPath(workbook));
    try {
        zip.open(utf8FromPath(workbook));
    } catch (const std::exception& e) {
        throw std::runtime_error("Couldn't open the spreadsheet. Is it an Excel (.xlsx) file?\n(" + std::string(e.what()) + ")");
    }
    if (!zip.hasEntry("xl/workbook.xml")) {
        zip.close();
        throw std::runtime_error("That file is not an Excel (.xlsx) workbook.");
    }
}

std::string joinPath(const std::string& baseDir, std::string target)  // baseDir "xl/" + "worksheets/s1.xml"
{
    if (!target.empty() && target[0] == '/') return target.substr(1);
    std::string path = baseDir + target;
    // Resolve "../".
    for (std::size_t p; (p = path.find("/../")) != std::string::npos;) {
        const std::size_t prev = path.rfind('/', p == 0 ? 0 : p - 1);
        path.erase(prev == std::string::npos ? 0 : prev + 1, p + 4 - (prev == std::string::npos ? 0 : prev + 1));
    }
    return path;
}

struct SheetLocation {
    std::string name;
    std::string path;  // e.g. "xl/worksheets/sheet1.xml"
};

SheetLocation findSheet(const OpenXLSX::XLZipArchive& zip)
{
    pugi::xml_document book, rels;
    parseXml(entry(zip, "xl/workbook.xml"), book, "workbook");
    parseXml(entry(zip, "xl/_rels/workbook.xml.rels"), rels, "workbook relationships");

    pugi::xml_node chosen;
    for (auto sheet : book.child("workbook").child("sheets").children("sheet")) {
        if (!chosen) chosen = sheet;
        if (lower(sheet.attribute("name").as_string()).rfind("all cases", 0) == 0) {
            chosen = sheet;
            break;
        }
    }
    if (!chosen) throw std::runtime_error("The workbook has no sheets.");
    const std::string rid = chosen.attribute("r:id").as_string();
    for (auto rel : rels.child("Relationships").children("Relationship")) {
        if (rid == rel.attribute("Id").as_string())
            return {chosen.attribute("name").as_string(), joinPath("xl/", rel.attribute("Target").as_string())};
    }
    throw std::runtime_error("The spreadsheet is damaged (sheet not found).");
}

std::vector<std::string> readSharedStrings(const OpenXLSX::XLZipArchive& zip)
{
    std::vector<std::string> out;
    if (!zip.hasEntry("xl/sharedStrings.xml")) return out;
    pugi::xml_document doc;
    parseXml(zip.getEntry("xl/sharedStrings.xml"), doc, "shared strings");
    for (auto si : doc.child("sst").children("si")) {
        std::string text;
        for (auto part : si.children()) {
            const std::string tag = part.name();
            if (tag == "t") {
                text += part.text().get();
            } else if (tag == "r") {
                text += part.child("t").text().get();
            }  // "rPh" (phonetic hints) is skipped
        }
        out.push_back(std::move(text));
    }
    return out;
}

std::string cellValue(const pugi::xml_node& c, const std::vector<std::string>& shared)
{
    const std::string type = c.attribute("t").as_string();
    if (type == "inlineStr") {
        std::string text;
        for (auto part : c.child("is").children()) {
            const std::string tag = part.name();
            if (tag == "t") text += part.text().get();
            else if (tag == "r") text += part.child("t").text().get();
        }
        return fixNewlines(text);
    }
    const std::string v = c.child("v").text().get();
    if (type == "s") {
        const auto i = static_cast<std::size_t>(std::strtoull(v.c_str(), nullptr, 10));
        return i < shared.size() ? fixNewlines(shared[i]) : std::string();
    }
    if (type == "b") return v == "1" ? "TRUE" : "FALSE";
    return fixNewlines(v);  // "str" (formula text), numbers, errors
}

struct RawRow {
    std::size_t number = 0;
    std::vector<std::string> cells;
    bool empty() const
    {
        return std::all_of(cells.begin(), cells.end(), [](const std::string& s) { return trimmed(s).empty(); });
    }
};

std::vector<RawRow> readRows(const pugi::xml_document& sheet, const std::vector<std::string>& shared)
{
    std::vector<RawRow> rows;
    std::size_t autoRow = 0;
    for (auto row : sheet.child("worksheet").child("sheetData").children("row")) {
        RawRow r;
        r.number = row.attribute("r") ? row.attribute("r").as_ullong() : autoRow + 1;
        autoRow = r.number;
        std::size_t autoCol = 0;
        for (auto c : row.children("c")) {
            const std::string ref = c.attribute("r").as_string();
            const std::size_t col = ref.empty() ? autoCol : columnIndex(ref);
            autoCol = col + 1;
            if (r.cells.size() <= col) r.cells.resize(col + 1);
            r.cells[col] = cellValue(c, shared);
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

void padTo(std::vector<std::string>& v, std::size_t n)
{
    if (v.size() < n) v.resize(n);
}

VacationSheet buildSheet(const std::string& name, const std::vector<RawRow>& raw)
{
    VacationSheet sheet;
    sheet.sheetName = name;
    const RawRow* header = nullptr;
    for (const auto& r : raw)
        if (r.number == 1) header = &r;
    if (!header) throw std::runtime_error("The \"" + name + "\" sheet is empty: row 1 should have the column names.");
    std::size_t count = 0;
    for (std::size_t i = 0; i < header->cells.size(); ++i)
        if (!trimmed(header->cells[i]).empty()) count = i + 1;
    if (count == 0) throw std::runtime_error("The \"" + name + "\" sheet has no column names in row 1.");
    for (std::size_t i = 0; i < count; ++i) sheet.headers.push_back(trimmed(header->cells[i]));

    for (const auto& r : raw) {
        if (r.number <= 1) continue;
        VacationRow row;
        row.row = r.number;
        row.values = r.cells;
        row.values.resize(count);
        const bool blank = std::all_of(row.values.begin(), row.values.end(),
                                       [](const std::string& s) { return trimmed(s).empty(); });
        if (!blank) sheet.rows.push_back(std::move(row));
    }
    return sheet;
}

}  // namespace

// ---- public ------------------------------------------------------------------------

std::string normalizeHeader(const std::string& text)
{
    std::string key;
    for (char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) key += static_cast<char>(std::tolower(u));
    }
    if (key.rfind("oc2", 0) == 0) key = "opc2" + key.substr(3);  // the attorney tabs say "OC 2 Address"
    return key;
}

int VacationSheet::column(const std::string& name) const
{
    const std::string key = normalizeHeader(name);
    for (std::size_t i = 0; i < headers.size(); ++i)
        if (normalizeHeader(headers[i]) == key) return static_cast<int>(i);
    return -1;
}

VacationSheet loadVacationSheet(const fs::path& workbook)
{
    OpenXLSX::XLZipArchive zip;
    openZip(zip, workbook);
    try {
        const SheetLocation where = findSheet(zip);
        const auto shared = readSharedStrings(zip);
        pugi::xml_document sheet;
        parseXml(entry(zip, where.path), sheet, "sheet");
        VacationSheet result = buildSheet(where.name, readRows(sheet, shared));
        zip.close();
        return result;
    } catch (...) {
        if (zip.isOpen()) zip.close();
        throw;
    }
}

namespace {

void setCellText(pugi::xml_node c, const std::string& text)
{
    while (c.first_child()) c.remove_child(c.first_child());
    c.remove_attribute("t");
    const std::string safe = xmlSafe(text);
    if (safe.empty()) return;
    c.append_attribute("t") = "inlineStr";
    auto t = c.append_child("is").append_child("t");
    t.append_attribute("xml:space") = "preserve";
    t.text().set(safe.c_str());
}

// The cell at `col` of `row`, created in column order if missing. A new cell takes the
// row's own style if it has one, else the style of the same column in `styleRow`.
pugi::xml_node getOrCreateCell(pugi::xml_node row, std::size_t col, pugi::xml_node styleRow)
{
    pugi::xml_node before;
    for (auto c : row.children("c")) {
        const std::size_t here = columnIndex(c.attribute("r").as_string());
        if (here == col) return c;
        if (here > col) {
            before = c;
            break;
        }
    }
    pugi::xml_node cell = before ? row.insert_child_before("c", before) : row.append_child("c");
    cell.append_attribute("r") = (columnLetters(col) + std::to_string(row.attribute("r").as_ullong())).c_str();
    if (row.attribute("s") && row.attribute("customFormat")) {
        cell.append_attribute("s") = row.attribute("s").as_string();
    } else if (styleRow) {
        for (auto c : styleRow.children("c")) {
            if (columnIndex(c.attribute("r").as_string()) == col) {
                if (c.attribute("s")) cell.append_attribute("s") = c.attribute("s").as_string();
                break;
            }
        }
    }
    return cell;
}

pugi::xml_node getOrCreateRow(pugi::xml_node data, std::size_t number, pugi::xml_node styleRow)
{
    pugi::xml_node before;
    for (auto r : data.children("row")) {
        const std::size_t here = r.attribute("r").as_ullong();
        if (here == number) return r;
        if (here > number) {
            before = r;
            break;
        }
    }
    pugi::xml_node row = before ? data.insert_child_before("row", before) : data.append_child("row");
    row.append_attribute("r") = static_cast<unsigned long long>(number);
    if (styleRow) {
        for (const char* name : {"s", "customFormat", "ht", "customHeight"})
            if (styleRow.attribute(name)) row.append_attribute(name) = styleRow.attribute(name).as_string();
    }
    return row;
}

void growRange(pugi::xml_attribute ref, std::size_t newLastRow)
{
    // "A1:N114" -> "A1:N<newLastRow>"
    std::string text = ref.as_string();
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) return;
    const std::size_t endRow = rowNumber(text.substr(colon + 1));
    if (endRow >= newLastRow) return;
    std::string endCol = text.substr(colon + 1);
    endCol.erase(std::find_if(endCol.begin(), endCol.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }),
                 endCol.end());
    ref.set_value((text.substr(0, colon + 1) + endCol + std::to_string(newLastRow)).c_str());
}

// Makes the sheet's tables cover a row added below them.
void extendTables(OpenXLSX::XLZipArchive& zip, const std::string& sheetPath, std::size_t lastRow)
{
    const std::size_t slash = sheetPath.rfind('/');
    const std::string dir = sheetPath.substr(0, slash + 1);
    const std::string relsName = dir + "_rels/" + sheetPath.substr(slash + 1) + ".rels";
    if (!zip.hasEntry(relsName)) return;
    pugi::xml_document rels;
    parseXml(zip.getEntry(relsName), rels, "sheet relationships");
    for (auto rel : rels.child("Relationships").children("Relationship")) {
        const std::string type = rel.attribute("Type").as_string();
        if (type.size() < 6 || type.compare(type.size() - 6, 6, "/table") != 0) continue;
        const std::string tablePath = joinPath(dir, rel.attribute("Target").as_string());
        if (!zip.hasEntry(tablePath)) continue;
        pugi::xml_document table;
        parseXml(zip.getEntry(tablePath), table, "table");
        auto root = table.child("table");
        growRange(root.attribute("ref"), lastRow);
        growRange(root.child("autoFilter").attribute("ref"), lastRow);
        zip.addEntry(tablePath, xmlText(table));
    }
}

}  // namespace

std::size_t saveVacationRow(const fs::path& workbook, std::size_t row, const std::vector<std::string>& expected,
                            const std::vector<std::string>& values)
{
    OpenXLSX::XLZipArchive zip;
    openZip(zip, workbook);
    const fs::path temp = fs::path(workbook).concat(".saving");
    std::size_t target = 0;
    try {
        const SheetLocation where = findSheet(zip);
        const auto shared = readSharedStrings(zip);
        pugi::xml_document sheetDoc;
        parseXml(entry(zip, where.path), sheetDoc, "sheet");
        const auto raw = readRows(sheetDoc, shared);
        const VacationSheet current = buildSheet(where.name, raw);
        const std::size_t count = current.headers.size();
        if (values.size() != count)
            throw std::runtime_error("The sheet's columns changed (it now has " + std::to_string(count) +
                                     " columns). Reload the list and try again.");

        // Where does it go, and is it still what the user saw?
        std::vector<std::string> existing(count);
        std::size_t lastUsed = 1;
        for (const auto& r : raw) {
            if (r.number > 1 && !r.empty()) lastUsed = std::max(lastUsed, r.number);
            if (r.number == row && row != 0) {
                existing = r.cells;
                existing.resize(count);
            }
        }
        if (row == 0) {
            target = lastUsed + 1;
        } else {
            target = row;
            if (target <= 1) throw std::runtime_error("Row 1 holds the column names and can't be edited here.");
            std::vector<std::string> was = expected;
            padTo(was, count);
            for (std::size_t i = 0; i < count; ++i) {
                if (fixNewlines(was[i]) != existing[i])
                    throw SheetConflict(
                        "This case was changed in the spreadsheet after you opened it (someone else may have edited "
                        "it). Nothing was saved. Reload the list, then make your change again.");
            }
        }

        // Patch the cells that changed.
        auto data = sheetDoc.child("worksheet").child("sheetData");
        pugi::xml_node styleRow;  // the nearest used row above, to copy formatting from
        for (auto r : data.children("row")) {
            const std::size_t n = r.attribute("r").as_ullong();
            if (n > 1 && n < target) {
                for (const auto& rr : raw)
                    if (rr.number == n && !rr.empty()) styleRow = r;
            }
        }
        auto rowNode = getOrCreateRow(data, target, styleRow);
        bool changed = false;
        for (std::size_t i = 0; i < count; ++i) {
            const std::string now = fixNewlines(values[i]);
            const std::string old = row == 0 ? std::string() : existing[i];
            if (now == old) continue;
            setCellText(getOrCreateCell(rowNode, i, styleRow), now);
            changed = true;
        }
        if (row == 0 && !changed) throw std::runtime_error("There is nothing to add: every box is empty.");

        if (target > lastUsed) {
            auto dim = sheetDoc.child("worksheet").child("dimension").attribute("ref");
            if (dim) growRange(dim, target);
            extendTables(zip, where.path, target);
        }
        if (changed) zip.addEntry(where.path, xmlText(sheetDoc));

        zip.save(utf8FromPath(temp));
        zip.close();
    } catch (...) {
        if (zip.isOpen()) zip.close();
        std::error_code ignore;
        fs::remove(temp, ignore);
        throw;
    }

    std::error_code ec;
    fs::rename(temp, workbook, ec);
    if (ec) {
        std::error_code ignore;
        fs::remove(temp, ignore);
        throw std::runtime_error(
            "Couldn't save to the spreadsheet. If it is open in Excel on this computer, close it and try again.\n(" +
            ec.message() + ")");
    }
    return target;
}

}  // namespace finrenamer

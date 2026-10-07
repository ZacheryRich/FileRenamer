// Writes the Deficiency List as a Word document (.docx) with no outside
// library. A .docx is a zip of XML files; Word reads zip entries that are
// stored uncompressed, so the zip writer here is just headers and checksums.
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "finrenamer/DeficiencyReport.h"

namespace fs = std::filesystem;

namespace finrenamer {
namespace {

// ---- Zip (stored entries only) ----------------------------------------------

std::uint32_t crc32(const std::string& data)
{
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char ch : data) crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void put16(std::string& out, std::uint16_t v)
{
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>(v >> 8);
}

void put32(std::string& out, std::uint32_t v)
{
    put16(out, static_cast<std::uint16_t>(v & 0xFFFF));
    put16(out, static_cast<std::uint16_t>(v >> 16));
}

class ZipWriter {
public:
    void add(const std::string& name, const std::string& data)
    {
        const std::uint32_t crc = crc32(data);
        const auto size = static_cast<std::uint32_t>(data.size());
        const auto offset = static_cast<std::uint32_t>(body_.size());

        // Local header, then the data.
        put32(body_, 0x04034b50);
        put16(body_, 20);  // version needed
        put16(body_, 0);   // flags
        put16(body_, 0);   // method: stored
        put16(body_, 0);   // time 00:00
        put16(body_, 0x21);  // date 1980-01-01
        put32(body_, crc);
        put32(body_, size);
        put32(body_, size);
        put16(body_, static_cast<std::uint16_t>(name.size()));
        put16(body_, 0);  // extra length
        body_ += name;
        body_ += data;

        // Central directory entry, written at the end.
        put32(directory_, 0x02014b50);
        put16(directory_, 20);  // made by
        put16(directory_, 20);  // needed
        put16(directory_, 0);
        put16(directory_, 0);
        put16(directory_, 0);
        put16(directory_, 0x21);
        put32(directory_, crc);
        put32(directory_, size);
        put32(directory_, size);
        put16(directory_, static_cast<std::uint16_t>(name.size()));
        put16(directory_, 0);  // extra
        put16(directory_, 0);  // comment
        put16(directory_, 0);  // disk
        put16(directory_, 0);  // internal attributes
        put32(directory_, 0);  // external attributes
        put32(directory_, offset);
        directory_ += name;
        ++count_;
    }

    std::string finish()
    {
        std::string out = body_;
        const auto directoryOffset = static_cast<std::uint32_t>(out.size());
        out += directory_;
        put32(out, 0x06054b50);
        put16(out, 0);
        put16(out, 0);
        put16(out, count_);
        put16(out, count_);
        put32(out, static_cast<std::uint32_t>(directory_.size()));
        put32(out, directoryOffset);
        put16(out, 0);  // comment length
        return out;
    }

private:
    std::string body_, directory_;
    std::uint16_t count_ = 0;
};

// ---- Word XML -----------------------------------------------------------------

std::string esc(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default:
            // XML 1.0 can't hold most control characters.
            if (static_cast<unsigned char>(c) >= 0x20 || c == '\t') out += c;
        }
    }
    return out;
}

const char* const kXmlHeader = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
const char* const kWordNamespace = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";

std::string contentTypes()
{
    return std::string(kXmlHeader) +
           "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
           "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
           "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
           "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
           "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
           "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
           "</Types>";
}

std::string packageRelationships()
{
    return std::string(kXmlHeader) +
           "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
           "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
           "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
           "</Relationships>";
}

std::string documentRelationships()
{
    return std::string(kXmlHeader) +
           "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
           "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
           "</Relationships>";
}

std::string coreProperties(const std::string& title)
{
    return std::string(kXmlHeader) +
           "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
           "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
           "<dc:title>" + esc(title) + "</dc:title><dc:creator>FinRenamer</dc:creator>"
           "</cp:coreProperties>";
}

std::string styles()
{
    return std::string(kXmlHeader) + "<w:styles " + kWordNamespace + ">"
           "<w:docDefaults>"
           "<w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:eastAsia=\"Calibri\" w:cs=\"Calibri\"/>"
           "<w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/><w:lang w:val=\"en-US\"/></w:rPr></w:rPrDefault>"
           "<w:pPrDefault><w:pPr><w:spacing w:after=\"120\" w:line=\"264\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault>"
           "</w:docDefaults>"
           "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/><w:basedOn w:val=\"Normal\"/>"
           "<w:next w:val=\"Normal\"/><w:qFormat/><w:pPr><w:keepNext/><w:spacing w:before=\"0\" w:after=\"60\"/><w:jc w:val=\"center\"/></w:pPr>"
           "<w:rPr><w:b/><w:sz w:val=\"44\"/><w:szCs w:val=\"44\"/><w:color w:val=\"1F2937\"/></w:rPr></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"Subtitle\"><w:name w:val=\"Subtitle\"/><w:basedOn w:val=\"Normal\"/>"
           "<w:next w:val=\"Normal\"/><w:qFormat/><w:pPr><w:spacing w:after=\"360\"/><w:jc w:val=\"center\"/></w:pPr>"
           "<w:rPr><w:sz w:val=\"28\"/><w:szCs w:val=\"28\"/><w:color w:val=\"4B5563\"/></w:rPr></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Normal\"/>"
           "<w:next w:val=\"Normal\"/><w:qFormat/><w:pPr><w:keepNext/><w:keepLines/><w:spacing w:before=\"360\" w:after=\"80\"/><w:outlineLvl w:val=\"1\"/></w:pPr>"
           "<w:rPr><w:b/><w:sz w:val=\"26\"/><w:szCs w:val=\"26\"/><w:color w:val=\"1F2937\"/></w:rPr></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"AccountNote\"><w:name w:val=\"Account Note\"/><w:basedOn w:val=\"Normal\"/>"
           "<w:next w:val=\"Normal\"/><w:qFormat/><w:pPr><w:keepNext/><w:spacing w:after=\"100\"/></w:pPr>"
           "<w:rPr><w:i/><w:sz w:val=\"20\"/><w:szCs w:val=\"20\"/><w:color w:val=\"4B5563\"/></w:rPr></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"TableText\"><w:name w:val=\"Table Text\"/><w:basedOn w:val=\"Normal\"/>"
           "<w:qFormat/><w:pPr><w:spacing w:before=\"40\" w:after=\"40\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr></w:style>"
           "</w:styles>";
}

std::string paragraph(const std::string& style, const std::string& text, bool keepNext = false, bool bold = false)
{
    return "<w:p><w:pPr><w:pStyle w:val=\"" + style + "\"/>" + (keepNext ? "<w:keepNext/>" : "") + "</w:pPr>"
           "<w:r>" + (bold ? "<w:rPr><w:b/></w:rPr>" : "") + "<w:t xml:space=\"preserve\">" + esc(text) +
           "</w:t></w:r></w:p>";
}

std::string cell(int width, const std::string& text, bool header, bool keepNext)
{
    return "<w:tc><w:tcPr><w:tcW w:w=\"" + std::to_string(width) + "\" w:type=\"dxa\"/>" +
           (header ? "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"E5E7EB\"/>" : "") + "</w:tcPr>" +
           paragraph("TableText", text, keepNext, header) + "</w:tc>";
}

std::string table(const ReportTable& t, bool showFound, bool showMissing)
{
    constexpr int kTotal = 9360;  // letter page, 1-inch margins, in twentieths of a point
    constexpr int kYear = 2200;
    const int columns = (showFound ? 1 : 0) + (showMissing ? 1 : 0);
    const int other = columns ? (kTotal - kYear) / columns : 0;

    std::string xml =
        "<w:tbl><w:tblPr><w:tblW w:w=\"" + std::to_string(kTotal) + "\" w:type=\"dxa\"/>"
        "<w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"9CA3AF\"/>"
        "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"9CA3AF\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"9CA3AF\"/>"
        "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"9CA3AF\"/>"
        "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"9CA3AF\"/>"
        "<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"9CA3AF\"/>"
        "</w:tblBorders><w:tblLayout w:type=\"fixed\"/>"
        "<w:tblCellMar><w:left w:w=\"110\" w:type=\"dxa\"/><w:right w:w=\"110\" w:type=\"dxa\"/></w:tblCellMar>"
        "</w:tblPr><w:tblGrid><w:gridCol w:w=\"" + std::to_string(kYear) + "\"/>";
    for (int i = 0; i < columns; ++i) xml += "<w:gridCol w:w=\"" + std::to_string(other) + "\"/>";
    xml += "</w:tblGrid>";

    auto row = [&](const std::string& year, const std::string& found, const std::string& missing, bool header,
                   bool keepNext) {
        std::string r = "<w:tr><w:trPr><w:cantSplit/>" + std::string(header ? "<w:tblHeader/>" : "") + "</w:trPr>";
        r += cell(kYear, year, header, keepNext);
        if (showFound) r += cell(other, found, header, keepNext);
        if (showMissing) r += cell(other, missing, header, keepNext);
        return r + "</w:tr>";
    };
    xml += row("Year", "Months Found", "Months Missing", true, true);
    for (std::size_t i = 0; i < t.rows.size(); ++i)
        xml += row(t.rows[i].year, t.rows[i].found, t.rows[i].missing, false, i + 1 < t.rows.size());
    return xml + "</w:tbl>";
}

std::string documentXml(const DeficiencyReport& report)
{
    std::string body = paragraph("Title", report.title) + paragraph("Subtitle", report.rangeLine);
    for (const ReportTable& t : report.tables) {
        body += "<w:p><w:pPr><w:pStyle w:val=\"Heading2\"/></w:pPr><w:r><w:t xml:space=\"preserve\">" +
                esc(t.heading) + "</w:t></w:r></w:p>";
        if (!t.note.empty()) body += paragraph("AccountNote", t.note);
        body += table(t, report.showFound, report.showMissing);
        body += "<w:p><w:pPr><w:spacing w:after=\"0\"/></w:pPr></w:p>";  // Word needs a paragraph after a table
    }
    body += "<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/>"
            "<w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" w:header=\"720\" "
            "w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>";
    return std::string(kXmlHeader) + "<w:document " + kWordNamespace + "><w:body>" + body + "</w:body></w:document>";
}

}  // namespace

std::string docxBytes(const DeficiencyReport& report)
{
    ZipWriter zip;
    zip.add("[Content_Types].xml", contentTypes());
    zip.add("_rels/.rels", packageRelationships());
    zip.add("word/document.xml", documentXml(report));
    zip.add("word/_rels/document.xml.rels", documentRelationships());
    zip.add("word/styles.xml", styles());
    zip.add("docProps/core.xml", coreProperties(report.title));
    return zip.finish();
}

void writeDocx(const fs::path& file, const DeficiencyReport& report)
{
    const std::string bytes = docxBytes(report);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) throw std::runtime_error("Couldn't write the file. Is it open in Word, or the folder read-only?");
}

}  // namespace finrenamer

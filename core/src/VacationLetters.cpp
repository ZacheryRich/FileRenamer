#include "finrenamer/VacationLetters.h"

#include <OpenXLSX.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

#include "finrenamer/Utf8Path.h"

namespace fs = std::filesystem;

namespace finrenamer {

namespace {

const char* kDocument = "word/document.xml";

std::string xmlEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default:
            if (u < 0x20 && c != '\t' && c != '\n') break;  // not allowed in XML
            out += c;
        }
    }
    return out;
}

std::string trim(const std::string& s)
{
    std::size_t b = 0, e = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Cell text as run content: line breaks become <w:br/>, tabs <w:tab/>.
std::string runContent(const std::string& value)
{
    std::string text = trim(value);
    std::string out;
    std::string line;
    auto flush = [&] {
        if (!line.empty()) out += "<w:t xml:space=\"preserve\">" + xmlEscape(line) + "</w:t>";
        line.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;
            flush();
            out += "<w:br/>";
        } else if (c == '\n') {
            flush();
            out += "<w:br/>";
        } else if (c == '\t') {
            flush();
            out += "<w:tab/>";
        } else {
            line += c;
        }
    }
    flush();
    return out;
}

// ---- finding fields in document.xml -----------------------------------------------

struct Field {
    std::size_t begin = 0;   // start of the run holding the "begin" mark
    std::size_t end = 0;     // just past the run holding the "end" mark
    std::size_t resultFrom = 0;  // just past the "separate" run (== end if none)
    std::string code;        // the instruction text, trimmed
    bool nested = false;
};

std::size_t runStart(const std::string& xml, std::size_t at)
{
    const std::size_t a = xml.rfind("<w:r>", at);
    const std::size_t b = xml.rfind("<w:r ", at);
    if (a == std::string::npos) return b;
    if (b == std::string::npos) return a;
    return std::max(a, b);
}

std::size_t runEnd(const std::string& xml, std::size_t at)
{
    const std::size_t e = xml.find("</w:r>", at);
    return e == std::string::npos ? std::string::npos : e + 6;
}

std::string instructionText(const std::string& xml, std::size_t from, std::size_t to)
{
    std::string code;
    std::size_t p = from;
    while (true) {
        p = xml.find("<w:instrText", p);
        if (p == std::string::npos || p >= to) break;
        const std::size_t open = xml.find('>', p);
        const std::size_t close = xml.find("</w:instrText>", open);
        if (open == std::string::npos || close == std::string::npos) break;
        code += xml.substr(open + 1, close - open - 1);
        p = close;
    }
    return trim(code);
}

// Finds the next complex field at or after `from`; false when there are no more.
bool nextField(const std::string& xml, std::size_t from, Field& f)
{
    const std::string kBegin = "w:fldCharType=\"begin\"";
    const std::size_t b = xml.find(kBegin, from);
    if (b == std::string::npos) return false;
    f = Field{};
    f.begin = runStart(xml, b);
    const std::size_t end = xml.find("w:fldCharType=\"end\"", b);
    if (f.begin == std::string::npos || end == std::string::npos) return false;
    f.end = runEnd(xml, end);
    if (f.end == std::string::npos) return false;
    const std::size_t nextBegin = xml.find(kBegin, b + kBegin.size());
    f.nested = nextBegin != std::string::npos && nextBegin < end;
    std::size_t sep = xml.find("w:fldCharType=\"separate\"", b);
    if (sep != std::string::npos && sep < end) {
        f.resultFrom = runEnd(xml, sep);
        f.code = instructionText(xml, b, sep);
    } else {
        f.resultFrom = f.end;
        f.code = instructionText(xml, b, end);
    }
    return true;
}

// "MERGEFIELD OPC_Address \* MERGEFORMAT" -> "OPC_Address"; "" if this isn't a merge field.
std::string mergeFieldName(const std::string& code)
{
    std::size_t p = 0;
    while (p < code.size() && code[p] == ' ') ++p;
    static const std::string kWord = "MERGEFIELD";
    if (code.size() < p + kWord.size() || code.compare(p, kWord.size(), kWord) != 0) return {};
    p += kWord.size();
    while (p < code.size() && code[p] == ' ') ++p;
    std::string name;
    if (p < code.size() && code[p] == '"') {
        const std::size_t q = code.find('"', p + 1);
        name = code.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1);
    } else {
        while (p < code.size() && code[p] != ' ') name += code[p++];
    }
    return trim(name);
}

bool isDateField(const std::string& code)
{
    std::size_t p = 0;
    while (p < code.size() && code[p] == ' ') ++p;
    for (const char* word : {"DATE", "TIME"}) {
        const std::size_t n = std::string(word).size();
        if (code.compare(p, n, word) == 0 && (code.size() == p + n || code[p + n] == ' ')) return true;
    }
    return false;
}

// The formatting of the field's result text (so a filled-in value looks like the placeholder did).
std::string resultProperties(const std::string& xml, const Field& f)
{
    const std::size_t from = f.resultFrom < f.end ? f.resultFrom : f.begin;
    const std::size_t to = f.resultFrom < f.end ? f.end : runEnd(xml, f.begin);
    const std::size_t a = xml.find("<w:rPr>", from);
    if (a == std::string::npos || a >= to) return {};
    const std::size_t b = xml.find("</w:rPr>", a);
    if (b == std::string::npos || b >= to) return {};
    return xml.substr(a, b + 8 - a);
}

// ---- one letter ------------------------------------------------------------------------

std::string lookup(const LetterValues& values, const std::string& fieldName, bool* found)
{
    const auto it = values.find(normalizeHeader(fieldName));
    if (found) *found = it != values.end();
    return it == values.end() ? std::string() : it->second;
}

// ---- signature pictures ----------------------------------------------------------------------------

constexpr long long kEmuPerInch = 914400;
constexpr long long kMaxWidthEmu = 22 * kEmuPerInch / 10;  // 2.2 in
constexpr long long kMaxHeightEmu = 8 * kEmuPerInch / 10;  // 0.8 in

struct StoredImage {
    std::string relId;
    std::string mediaName;  // word/media/<name>
    std::string extension;  // "png" / "jpeg"
    long long cx = 0, cy = 0;
    std::string bytes;
};

class ImageStore {
public:
    // The <w:r> holding the picture for `file`, added to the store the first time it is used.
    std::string run(const fs::path& file)
    {
        const std::string key = caseFoldKey(file);
        auto it = byFile_.find(key);
        if (it == byFile_.end()) {
            StoredImage img;
            const ImageSize size = imageSize(file);
            std::ifstream in(file, std::ios::binary);
            img.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            std::string ext = utf8FromPath(file.extension());
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            img.extension = ext == ".png" ? "png" : "jpeg";
            const std::size_t n = images_.size() + 1;
            img.relId = "rIdSignature" + std::to_string(n);
            img.mediaName = "signature" + std::to_string(n) + "." + img.extension;
            // Fit inside the box, keeping the proportions.
            long long cx = kMaxWidthEmu;
            long long cy = cx * size.height / size.width;
            if (cy > kMaxHeightEmu) {
                cy = kMaxHeightEmu;
                cx = cy * size.width / size.height;
            }
            img.cx = std::max<long long>(cx, 1);
            img.cy = std::max<long long>(cy, 1);
            images_.push_back(std::move(img));
            it = byFile_.emplace(key, images_.size() - 1).first;
        }
        const StoredImage& img = images_[it->second];
        const std::string id = std::to_string(nextId_++);
        const std::string cx = std::to_string(img.cx), cy = std::to_string(img.cy);
        return "<w:r><w:drawing>"
               "<wp:inline xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
               "distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
               "<wp:extent cx=\"" + cx + "\" cy=\"" + cy + "\"/>"
               "<wp:docPr id=\"" + id + "\" name=\"Signature " + id + "\"/>"
               "<wp:cNvGraphicFramePr><a:graphicFrameLocks xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
               "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
               "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
               "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
               "<pic:nvPicPr><pic:cNvPr id=\"0\" name=\"" + xmlEscape(img.mediaName) + "\"/><pic:cNvPicPr/></pic:nvPicPr>"
               "<pic:blipFill><a:blip xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:embed=\"" + img.relId + "\"/>"
               "<a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
               "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" + cx + "\" cy=\"" + cy + "\"/></a:xfrm>"
               "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>"
               "</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>";
    }

    const std::vector<StoredImage>& images() const { return images_; }

private:
    std::vector<StoredImage> images_;
    std::map<std::string, std::size_t> byFile_;
    int nextId_ = 9001;  // picture ids must be unique in the document
};

bool isSignatureField(const std::string& name)
{
    return normalizeHeader(name) == "signature";
}

std::string fillBody(const std::string& body, const LetterValues& values, const std::string& todayText,
                     std::set<std::string>& missing, const fs::path& signature, ImageStore& images,
                     bool& sawSignatureField, bool& addedSignature)
{
    std::string out;
    std::size_t pos = 0;
    Field f;
    while (nextField(body, pos, f) && f.begin >= pos) {
        out.append(body, pos, f.begin - pos);
        const std::string span = body.substr(f.begin, f.end - f.begin);
        const std::string name = f.nested ? std::string() : mergeFieldName(f.code);
        if (!name.empty() && isSignatureField(name)) {
            sawSignatureField = true;
            if (!signature.empty()) {
                out += images.run(signature);
                addedSignature = true;
            } else {
                out += "<w:r>" + resultProperties(body, f) + "</w:r>";
            }
        } else if (!name.empty()) {
            bool found = false;
            const std::string value = lookup(values, name, &found);
            if (!found) missing.insert(name);
            out += "<w:r>" + resultProperties(body, f) + runContent(value) + "</w:r>";
        } else if (!f.nested && isDateField(f.code) && f.resultFrom < f.end) {
            // Keep the field (Word refreshes it) but show today's date until then.
            const std::size_t t = span.find("<w:t", f.resultFrom - f.begin);
            const std::size_t open = t == std::string::npos ? t : span.find('>', t);
            const std::size_t close = open == std::string::npos ? open : span.find("</w:t>", open);
            if (close == std::string::npos) {
                out += span;
            } else {
                std::string fixed = span;
                // make sure the new text keeps its spaces
                fixed.replace(open + 1, close - open - 1, xmlEscape(todayText));
                out += fixed;
            }
        } else {
            out += span;
        }
        pos = f.end;
    }
    out.append(body, pos, std::string::npos);
    return out;
}

// <w:pageBreakBefore/> on the first paragraph, so the next letter starts on a new page
// without an extra blank paragraph that could push onto a page of its own.
std::string startOnNewPage(const std::string& body)
{
    const std::size_t p = body.find("<w:p");
    if (p == std::string::npos) return body;
    std::size_t at = p;
    while (at < body.size() && body[at] != '>') ++at;  // end of the <w:p ...> tag
    if (at >= body.size()) return body;
    ++at;
    std::string out = body;
    if (out.compare(at, 7, "<w:pPr>") == 0) {
        std::size_t ins = at + 7;
        // The schema puts these three before pageBreakBefore.
        for (bool again = true; again;) {
            again = false;
            for (const char* lead : {"<w:pStyle ", "<w:keepNext", "<w:keepLines"}) {
                const std::size_t n = std::string(lead).size();
                if (out.compare(ins, n, lead) == 0) {
                    const std::size_t close = out.find("/>", ins);
                    if (close != std::string::npos) {
                        ins = close + 2;
                        again = true;
                    }
                }
            }
        }
        out.insert(ins, "<w:pageBreakBefore/>");
    } else {
        out.insert(at, "<w:pPr><w:pageBreakBefore/></w:pPr>");
    }
    return out;
}

void openTemplate(OpenXLSX::XLZipArchive& zip, const fs::path& file)
{
    std::error_code ec;
    if (!fs::exists(file, ec)) throw std::runtime_error("The letter template was not found:\n" + utf8FromPath(file));
    try {
        zip.open(utf8FromPath(file));
    } catch (const std::exception& e) {
        throw std::runtime_error("Couldn't open the letter template. Is it a Word (.docx) file?\n(" + std::string(e.what()) + ")");
    }
    if (!zip.hasEntry(kDocument)) {
        zip.close();
        throw std::runtime_error("The letter template is not a Word (.docx) document.");
    }
}

struct Body {
    std::string head;     // up to and including <w:body>
    std::string content;  // the letter
    std::string tail;     // the section settings and the closing tags
};

Body splitDocument(const std::string& xml)
{
    const std::size_t open = xml.find("<w:body>");
    const std::size_t close = xml.rfind("</w:body>");
    if (open == std::string::npos || close == std::string::npos)
        throw std::runtime_error("The letter template has no body text.");
    std::size_t sect = xml.rfind("<w:sectPr", close);
    if (sect == std::string::npos || sect < open) sect = close;
    Body b;
    b.head = xml.substr(0, open + 8);
    b.content = xml.substr(open + 8, sect - open - 8);
    b.tail = xml.substr(sect);
    return b;
}

}  // namespace

ImageSize imageSize(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("The signature picture could not be opened:\n" + utf8FromPath(file));
    std::string head(24, '\0');
    in.read(head.data(), 24);
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(head[i]); };
    if (in.gcount() >= 24 && head.compare(1, 3, "PNG") == 0 && byte(0) == 0x89) {
        ImageSize s;
        s.width = static_cast<int>((byte(16) << 24) | (byte(17) << 16) | (byte(18) << 8) | byte(19));
        s.height = static_cast<int>((byte(20) << 24) | (byte(21) << 16) | (byte(22) << 8) | byte(23));
        if (s.width > 0 && s.height > 0) return s;
    } else if (in.gcount() >= 4 && byte(0) == 0xFF && byte(1) == 0xD8) {
        in.clear();
        in.seekg(2);
        for (;;) {
            int c = in.get();
            while (c != EOF && c != 0xFF) c = in.get();
            int marker = in.get();
            while (marker == 0xFF) marker = in.get();
            if (c == EOF || marker == EOF) break;
            if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;  // no length
            const int len = (in.get() << 8) | in.get();
            const bool frame = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
            if (frame) {
                in.get();  // precision
                ImageSize s;
                s.height = (in.get() << 8) | in.get();
                s.width = (in.get() << 8) | in.get();
                if (s.width > 0 && s.height > 0) return s;
                break;
            }
            in.seekg(len - 2, std::ios::cur);
            if (!in) break;
        }
    }
    throw std::runtime_error("The signature picture must be a PNG or JPEG file that can be read:\n" + utf8FromPath(file));
}

std::string signerName(const LetterValues& letter)
{
    for (const char* key : {"partneroncase", "attorney"}) {
        const auto it = letter.find(key);
        if (it != letter.end() && !normalizeHeader(it->second).empty()) return trim(it->second);
    }
    return {};
}

fs::path signatureFor(const std::vector<SignatureEntry>& entries, const std::string& signer)
{
    const std::string wanted = normalizeHeader(signer);
    if (wanted.empty()) return {};
    for (const SignatureEntry& e : entries)
        if (normalizeHeader(e.attorney) == wanted) return e.file;
    return {};
}

LetterValues letterValues(const VacationSheet& sheet, const VacationRow& row)
{
    LetterValues v;
    for (std::size_t i = 0; i < sheet.headers.size() && i < row.values.size(); ++i)
        v[normalizeHeader(sheet.headers[i])] = row.values[i];
    return v;
}

std::vector<std::string> templateMergeFields(const fs::path& templateDocx)
{
    OpenXLSX::XLZipArchive zip;
    openTemplate(zip, templateDocx);
    std::string xml;
    try {
        xml = zip.getEntry(kDocument);
    } catch (...) {
        zip.close();
        throw;
    }
    zip.close();
    std::vector<std::string> names;
    std::size_t pos = 0;
    Field f;
    while (nextField(xml, pos, f) && f.begin >= pos) {
        const std::string name = f.nested ? std::string() : mergeFieldName(f.code);
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
        pos = f.end;
    }
    return names;
}

LetterResult writeCombinedLetters(const fs::path& templateDocx, const fs::path& output,
                                  const std::vector<LetterValues>& letters, const std::string& todayText,
                                  const std::vector<fs::path>& signatures)
{
    if (letters.empty()) throw std::runtime_error("No cases were chosen.");

    OpenXLSX::XLZipArchive zip;
    openTemplate(zip, templateDocx);
    const fs::path temp = fs::path(output).concat(".saving");
    LetterResult result;
    try {
        const Body body = splitDocument(zip.getEntry(kDocument));

        std::set<std::string> missing;
        std::string merged = body.head;
        ImageStore images;
        bool sawSignatureField = false;
        for (std::size_t i = 0; i < letters.size(); ++i) {
            bool added = false;
            const fs::path signature = i < signatures.size() ? signatures[i] : fs::path();
            std::string one = fillBody(body.content, letters[i], todayText, missing, signature, images,
                                       sawSignatureField, added);
            if (added) ++result.signaturesAdded;
            if (i > 0) one = startOnNewPage(one);
            merged += one;
        }
        merged += body.tail;
        zip.addEntry(kDocument, merged);
        result.templateHasSignatureField = sawSignatureField;

        if (!images.images().empty()) {
            const std::string relsName = "word/_rels/document.xml.rels";
            std::string rels = zip.hasEntry(relsName) ? zip.getEntry(relsName) : std::string();
            std::string types = zip.getEntry("[Content_Types].xml");
            std::string newRels;
            std::set<std::string> extensions;
            for (const StoredImage& img : images.images()) {
                zip.addEntry("word/media/" + img.mediaName, img.bytes);
                newRels += "<Relationship Id=\"" + img.relId +
                           "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                           "Target=\"media/" + img.mediaName + "\"/>";
                extensions.insert(img.extension);
            }
            const std::size_t closeRels = rels.rfind("</Relationships>");
            if (closeRels == std::string::npos)
                throw std::runtime_error("The letter template is missing its relationships part.");
            rels.insert(closeRels, newRels);
            zip.addEntry(relsName, rels);
            for (const std::string& ext : extensions) {
                if (types.find("Extension=\"" + ext + "\"") != std::string::npos) continue;
                const std::size_t open = types.find("<Types");
                const std::size_t gt = open == std::string::npos ? open : types.find('>', open);
                if (gt == std::string::npos) throw std::runtime_error("The letter template has no content types part.");
                types.insert(gt + 1, "<Default Extension=\"" + ext + "\" ContentType=\"image/" + ext + "\"/>");
            }
            zip.addEntry("[Content_Types].xml", types);
        }

        // Drop the template's mail-merge link to the Dropbox spreadsheet, so Word doesn't ask about it.
        if (zip.hasEntry("word/settings.xml")) {
            std::string settings = zip.getEntry("word/settings.xml");
            const std::size_t a = settings.find("<w:mailMerge>");
            const std::size_t b = settings.find("</w:mailMerge>");
            if (a != std::string::npos && b != std::string::npos && b > a) {
                settings.erase(a, b + 14 - a);
                zip.addEntry("word/settings.xml", settings);
            }
        }
        if (zip.hasEntry("word/_rels/settings.xml.rels")) {
            const std::string rels = zip.getEntry("word/_rels/settings.xml.rels");
            if (rels.find("mailMergeSource") != std::string::npos || rels.find("recipientData") != std::string::npos)
                zip.deleteEntry("word/_rels/settings.xml.rels");
        }
        if (zip.hasEntry("word/recipientData.xml")) {
            zip.deleteEntry("word/recipientData.xml");
            std::string types = zip.getEntry("[Content_Types].xml");
            const std::size_t a = types.find("<Override PartName=\"/word/recipientData.xml\"");
            if (a != std::string::npos) {
                const std::size_t b = types.find("/>", a);
                if (b != std::string::npos) {
                    types.erase(a, b + 2 - a);
                    zip.addEntry("[Content_Types].xml", types);
                }
            }
        }

        zip.save(utf8FromPath(temp));
        zip.close();
        result.letters = letters.size();
        result.missingColumns.assign(missing.begin(), missing.end());
    } catch (...) {
        if (zip.isOpen()) zip.close();
        std::error_code ignore;
        fs::remove(temp, ignore);
        throw;
    }

    std::error_code ec;
    fs::rename(temp, output, ec);
    if (ec) {
        std::error_code ignore;
        fs::remove(temp, ignore);
        throw std::runtime_error("Couldn't save the letters. If a file with that name is open in Word, close it or "
                                 "choose another name.\n(" + ec.message() + ")");
    }
    return result;
}

}  // namespace finrenamer

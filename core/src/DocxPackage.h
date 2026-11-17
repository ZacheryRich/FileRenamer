#pragma once

// Internal helpers shared by the Word template fillers (VacationLetters, WordTemplate):
// signature pictures added to a .docx package, and dropping a mail-merge data-source link.
// Not part of the public core API.

#include <OpenXLSX.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "finrenamer/Utf8Path.h"
#include "finrenamer/VacationLetters.h"  // imageSize

namespace finrenamer::docx {

namespace fs = std::filesystem;

inline std::string escapeXml(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default:
            if (u < 0x20 && c != '\t' && c != '\n') break;  // not allowed in XML
            out += c;
        }
    }
    return out;
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
               "<pic:nvPicPr><pic:cNvPr id=\"0\" name=\"" + escapeXml(img.mediaName) + "\"/><pic:cNvPicPr/></pic:nvPicPr>"
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


// Puts the store's pictures into the package: media files, relationships of document.xml and
// content types. Pictures must be used in document.xml (not a header or footer).
inline void addImagesToPackage(OpenXLSX::XLZipArchive& zip, const ImageStore& images)
{
    if (images.images().empty()) return;
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
        throw std::runtime_error("The template is missing its relationships part.");
    rels.insert(closeRels, newRels);
    zip.addEntry(relsName, rels);
    for (const std::string& ext : extensions) {
        if (types.find("Extension=\"" + ext + "\"") != std::string::npos) continue;
        const std::size_t open = types.find("<Types");
        const std::size_t gt = open == std::string::npos ? open : types.find('>', open);
        if (gt == std::string::npos) throw std::runtime_error("The template has no content types part.");
        types.insert(gt + 1, "<Default Extension=\"" + ext + "\" ContentType=\"image/" + ext + "\"/>");
    }
    zip.addEntry("[Content_Types].xml", types);

}

// Drops a template's mail-merge link to its data source (settings.xml mailMerge, its relationship
// and recipientData.xml), so Word doesn't ask about a data source when the document is opened.
inline void dropMailMergeLink(OpenXLSX::XLZipArchive& zip)
{
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

}

}  // namespace finrenamer::docx

#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "finrenamer/VacationSheet.h"

namespace finrenamer {

// Vacation letters: the firm's Word template (a mail-merge letter saved in a shared
// place) is filled in once per chosen case and the letters are stacked, one per page,
// in a single new .docx. The template file is only read.

// What goes into one letter: normalizeHeader(column name) -> cell text.
using LetterValues = std::map<std::string, std::string>;

LetterValues letterValues(const VacationSheet& sheet, const VacationRow& row);

struct LetterResult {
    std::size_t letters = 0;
    std::size_t signaturesAdded = 0;
    bool templateHasSignatureField = false;  // the template has a "Signature" merge field
    std::vector<std::string> missingColumns;  // merge fields in the template with no matching sheet column
};

// The merge fields in the template, in order of first appearance (names as written, e.g. "OPC_Firm_Name").
// Throws std::runtime_error with a message for the user if the template can't be read.
std::vector<std::string> templateMergeFields(const std::filesystem::path& templateDocx);

// Makes `output` from the template with one letter per entry of `letters`.
//  - Each merge field is replaced by the matching value (see normalizeHeader); line breaks in a
//    cell become line breaks in the letter. Letterhead, footer, fonts and layout come from the template.
//  - The date field keeps working and shows `todayText`.
//  - The template's link to its data source is dropped, so Word doesn't ask about it.
// Throws std::runtime_error with a message for the user (no letters, template unreadable, output
// can't be written -- most often because it is open in Word).
//  - A merge field named "Signature" is replaced by the picture in `signatures[i]` (parallel to
//    `letters`; an empty path, or a shorter list, means no picture for that letter and the field
//    is left blank). PNG and JPEG files are accepted; the picture is scaled to fit a box of about
//    2.2 x 0.8 inches, keeping its proportions. Throws std::runtime_error if a picture can't be read.
LetterResult writeCombinedLetters(const std::filesystem::path& templateDocx, const std::filesystem::path& output,
                                  const std::vector<LetterValues>& letters, const std::string& todayText,
                                  const std::vector<std::filesystem::path>& signatures = {});

// Attorneys and their signature pictures (kept by the app; add or remove people as the firm changes).
struct SignatureEntry {
    std::string attorney;          // as written in the spreadsheet, e.g. "Jane Doe"
    std::filesystem::path file;    // PNG or JPEG
};

// Who signs a letter: the "Partner on case" column (the name printed under the signature), or the
// "Attorney" column when that is blank. Empty if neither has a name.
std::string signerName(const LetterValues& letter);

// The picture for that signer, or an empty path. Names match ignoring case, spaces and punctuation,
// so "Jane Doe:" finds "jane doe".
std::filesystem::path signatureFor(const std::vector<SignatureEntry>& entries, const std::string& signer);

// Pixel size of a PNG or JPEG file; throws std::runtime_error (message for the user) otherwise.
struct ImageSize { int width = 0; int height = 0; };
ImageSize imageSize(const std::filesystem::path& file);

}  // namespace finrenamer

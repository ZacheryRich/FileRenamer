#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace finrenamer {

// Fills a Word (.docx) template made with merge fields (Insert > Quick Parts > Field > MergeField).
// The template is only read; the result is a new file.
//
//  - A merge field is replaced by its value, keeping the field's formatting. Names match ignoring
//    case, spaces and punctuation (normalizeHeader), so "OPC_BarNumber" = "OPC Bar Number".
//    Word's switches work: \* Upper, \* Lower, \* Caps, \* FirstCap, and \b "before" / \f "after"
//    (text added only when the value isn't blank). Line breaks in a value become line breaks.
//  - A paragraph left with no text because its fields were blank is removed (like Word's
//    "don't print blank lines"), unless it is the only paragraph of a table cell.
//  - Repeating blocks: everything from a "Begin_<Group>" merge field to the matching
//    "End_<Group>" field is repeated once per item of the group (and removed if it has none).
//    Begin/End may be in the same paragraph, in different paragraphs, or in different rows of
//    a table (the rows are repeated). Blocks may be nested. Inside a block, a field is looked up
//    in the block's item first, then in the enclosing blocks, then in `fields`. A paragraph
//    holding nothing but a Begin/End field is removed.
//  - DATE/TIME fields are kept (Word updates them) and show `todayText` until then.
//  - A merge field named "Signature" in the body is replaced by the picture `signature` (PNG or
//    JPEG), or left blank if there is none.
//  - Headers and footers are filled too (fields and blocks; no signature picture there).
//  - A mail-merge link to a data source is dropped, so Word doesn't ask about one.

using MergeRecord = std::map<std::string, std::string>;  // normalizeHeader(field name) -> value

struct MergeData {
    MergeRecord fields;                                       // the document's own fields
    std::map<std::string, std::vector<MergeRecord>> groups;   // normalizeHeader(group name) -> items
    std::filesystem::path signature;                          // picture for a "Signature" field (optional)
};

struct TemplateFillResult {
    std::vector<std::string> unknownFields;  // merge fields with no value of that name (left blank), as written
    std::vector<std::string> unknownGroups;  // Begin_ blocks naming a group that doesn't exist (removed)
    std::vector<std::string> problems;       // e.g. a Begin_ with no End_ (for the user)
    bool signatureField = false;
    bool signatureAdded = false;
};

// Throws std::runtime_error with a message for the user (template missing or not a .docx, output
// can't be written -- usually because a file of that name is open in Word).
TemplateFillResult fillWordTemplate(const std::filesystem::path& templateDocx, const std::filesystem::path& output,
                                    const MergeData& data, const std::string& todayText);

// The merge field names a template uses (body, headers, footers), as written, in order of first
// appearance; Begin_/End_ markers included. Throws like fillWordTemplate.
std::vector<std::string> wordTemplateFields(const std::filesystem::path& templateDocx);

// Word's \* text switches, for UTF-8 text (ASCII and Latin-1 letters).
std::string toUpperText(const std::string& s);
std::string toLowerText(const std::string& s);

}  // namespace finrenamer

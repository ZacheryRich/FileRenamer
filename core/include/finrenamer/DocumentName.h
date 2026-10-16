#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "finrenamer/DateSpec.h"

namespace finrenamer {

// What kind of file a PDF is. Statements are named from an account (see
// FilenameBuilder.h); a Template file is named from a user-defined pattern and the
// text typed into its fields; Other is a one-off "label (description)".
enum class DocKind { Statement, Template, Other };

// A user-defined name pattern for files that are not statements (emails, texts, letters,
// court orders...). The date always comes first; the pattern describes the rest.
//
//   Email from {Sender} to {Recipient}[ ({Description})]
//
//   {Name}   a box the user types into; required unless it is inside [ ]
//   [ ... ]  an optional part: left out (with its fixed words) when every box in it is blank.
//            If only some of its boxes are filled, the others are required.
//   \x       a literal character (\[ \] \{ \} \\)
//   other    fixed text, copied as it is
//
// Field names match each other ignoring letter case, so {Sender} twice is one box.
struct DocumentTemplate {
    std::int64_t id = 0;    // 0 = not saved yet
    std::string name;       // shown in the Type list, e.g. "Email"
    std::string pattern;
    bool allowPeriod = false;  // offer a period (date range) as well as a single date
    bool useDate = true;       // start the name with a date; false = no date box, no date in the name
};

struct TemplateField {
    std::string name;       // as first written in the pattern
    bool optional = false;  // every use is inside [ ]
};

// An error message for a pattern or template, or nullopt if it is usable.
std::optional<std::string> validatePattern(std::string_view pattern);
std::optional<std::string> validateTemplate(const DocumentTemplate& t);  // name + pattern

// The boxes a pattern asks for, in order of first use, each once. Empty if the pattern is invalid.
std::vector<TemplateField> templateFields(std::string_view pattern);

// Key under which a field's text is stored: trimmed, lower case, spaces collapsed.
std::string fieldKey(std::string_view name);

// The pattern filled with its own field names ("Email from Sender to Recipient (Description)"),
// to show the user what a template will produce.
std::string sampleLabel(std::string_view pattern);

// The ready-made templates offered on an empty list: Email, Text, Group Text...
std::vector<DocumentTemplate> exampleTemplates();

// Templates as a small XML file, to share with a colleague or keep as a backup.
// Ids are not written. templatesFromXml reads such a file: each template is checked with
// validateTemplate; the ones that fail are left out and described in `problems`
// (user-facing text). A file that isn't one of ours gives no templates and one problem.
std::string templatesToXml(const std::vector<DocumentTemplate>& templates);
struct TemplateImport {
    std::vector<DocumentTemplate> templates;  // valid ones, ids 0, names unique ignoring case
    std::vector<std::string> problems;
};
TemplateImport templatesFromXml(const std::string& xml);

// Everything the user types for a file that is not a statement.
//
//   Template  tmpl + fields (key -> text)
//   Other     "<label> (<description>)"
//
// The description of Other is always optional; its parentheses are left out when blank.
struct DocumentInfo {
    DocKind kind = DocKind::Statement;  // Statement = not a document; the row's account is used instead
    std::string label;                  // Other
    std::string description;            // Other
    bool noDate = false;                // Other: leave the date out of the name
    DocumentTemplate tmpl;              // Template (a copy, so the row stays valid if the template is edited)
    std::map<std::string, std::string> fields;  // Template: fieldKey -> text

    // Statement kind: nothing here is used.
    bool isBlank() const { return kind == DocKind::Statement; }

    std::string field(std::string_view name) const;
    void setField(std::string_view name, std::string value);
};

// The name without the date and extension: "Email from John to Jane (Re pickup)".
// Every piece is cleaned for Windows (sanitizeComponent). Empty for Statement.
std::string documentLabel(const DocumentInfo& info);

// Does the name start with a date? Statements always; a template if it says so; Other unless
// the user turned the date off for that file.
bool documentUsesDate(const DocumentInfo& info);

// "2026.01.31 Email from John to Jane (Re pickup).pdf". Without a date (or when the kind doesn't use
// one) the name is just the label: "Court Order.pdf".
std::string buildDocumentFilename(const DocumentInfo& info, const std::optional<DateSpec>& date,
                                  std::string_view extension = ".pdf");

// What still has to be entered, as "Needs sender, recipient and a date" (nullopt =
// complete). `dateMissing` adds the date to the list (ignored when the kind uses no date). Statement kind is never
// complete here: it is named from an account.
std::optional<std::string> documentMissing(const DocumentInfo& info, bool dateMissing = false);

// Which dates a kind accepts: a template a single date (or a period too if it allows it);
// Other a single date or a period; statements anything. Returns an error message,
// or nullopt if the date is allowed.
std::optional<std::string> validateDocumentDate(const DocumentInfo& info, const DateSpec& date);

// The single-date / period / quarter choices a kind offers.
struct DateModes {
    bool single = true;
    bool period = true;
    bool quarter = true;
};
DateModes allowedDateModes(const DocumentInfo& info);

// The fields of a Template row that carry-forward leaves blank (the optional ones).
void clearOptionalFields(DocumentInfo& info);

}  // namespace finrenamer

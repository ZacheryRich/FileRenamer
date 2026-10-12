#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "finrenamer/DateSpec.h"

namespace finrenamer {

// What kind of file a PDF is. Statements are named from an account (see
// FilenameBuilder.h); the others are named from the text the user types.
enum class DocKind { Statement, Email, Text, Letter, Other };

// A "Text" is one message, a conversation between two people, or a group chat.
enum class TextStyle { Single, Conversation, Group };

// Label shown first in the box when a kind is chosen (the user may change it).
inline constexpr std::string_view kDefaultTextLabel = "Texts";
inline constexpr std::string_view kDefaultLetterLabel = "Ltr";

// Everything the user types for a file that is not a statement. Which fields are
// used depends on the kind:
//
//   Email    "Email from <sender> to <recipient> (<description>)"       label fixed
//   Text     single:        "<label> from <sender> to <recipient> (...)"
//            conversation:  "<label> between <sender> and <recipient> (...)"
//            group:         "<label> with <sender> (...)"   sender = the people or group name
//   Letter   "<label> from <sender> (<description>)"
//   Other    "<label> (<description>)"
//
// The description is always optional; its parentheses are left out when it is blank.
struct DocumentInfo {
    DocKind kind = DocKind::Statement;  // Statement = not a document; the row's account is used instead
    TextStyle textStyle = TextStyle::Single;
    std::string label;
    std::string sender;
    std::string recipient;
    std::string description;

    // Statement kind with nothing typed.
    bool isBlank() const;
};

// The name without the date and extension: "Email from John to Jane (Re pickup)".
// Every piece is cleaned for Windows (sanitizeComponent). Empty for Statement.
std::string documentLabel(const DocumentInfo& info);

// "2026.01.31 Email from John to Jane (Re pickup).pdf"
std::string buildDocumentFilename(const DocumentInfo& info, const DateSpec& date, std::string_view extension = ".pdf");

// What still has to be entered, as "Needs sender, recipient and a date" (nullopt =
// complete). `dateMissing` adds the date to the list. Statement kind is never
// complete here: it is named from an account.
std::optional<std::string> documentMissing(const DocumentInfo& info, bool dateMissing = false);

// Which dates a kind accepts: emails, letters and single texts a single date;
// conversations, group chats and "other" a single date or a period; statements
// anything. Returns an error message, or nullopt if the date is allowed.
std::optional<std::string> validateDocumentDate(const DocumentInfo& info, const DateSpec& date);

// The single-date / period / quarter choices a kind offers.
struct DateModes {
    bool single = true;
    bool period = true;
    bool quarter = true;
};
DateModes allowedDateModes(const DocumentInfo& info);

}  // namespace finrenamer

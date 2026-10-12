#include "finrenamer/DocumentName.h"

#include <vector>

#include "finrenamer/FilenameBuilder.h"

namespace finrenamer {

namespace {

bool blank(const std::string& s)
{
    return sanitizeComponent(s).empty();
}

std::string joinNeeds(const std::vector<std::string>& needs)
{
    std::string out = "Needs ";
    for (std::size_t i = 0; i < needs.size(); ++i) {
        if (i > 0) out += i + 1 == needs.size() ? " and " : ", ";
        out += needs[i];
    }
    return out;
}

}  // namespace

bool DocumentInfo::isBlank() const
{
    return kind == DocKind::Statement && label.empty() && sender.empty() && recipient.empty() && description.empty();
}

std::string documentLabel(const DocumentInfo& info)
{
    const std::string label = sanitizeComponent(info.label);
    const std::string sender = sanitizeComponent(info.sender);
    const std::string recipient = sanitizeComponent(info.recipient);
    const std::string description = sanitizeComponent(info.description);

    std::string out;
    switch (info.kind) {
    case DocKind::Statement: return {};
    case DocKind::Email: out = "Email from " + sender + " to " + recipient; break;
    case DocKind::Text:
        switch (info.textStyle) {
        case TextStyle::Single: out = label + " from " + sender + " to " + recipient; break;
        case TextStyle::Conversation: out = label + " between " + sender + " and " + recipient; break;
        case TextStyle::Group: out = label + " with " + sender; break;
        }
        break;
    case DocKind::Letter: out = label + " from " + sender; break;
    case DocKind::Other: out = label; break;
    }
    if (!description.empty()) out += " (" + description + ")";
    return sanitizeComponent(out);
}

std::string buildDocumentFilename(const DocumentInfo& info, const DateSpec& date, std::string_view extension)
{
    return formatDateSpec(date) + " " + documentLabel(info) + std::string(extension);
}

std::optional<std::string> documentMissing(const DocumentInfo& info, bool dateMissing)
{
    std::vector<std::string> needs;
    switch (info.kind) {
    case DocKind::Statement: return std::string("Needs an account");
    case DocKind::Email:
        if (blank(info.sender)) needs.push_back("sender");
        if (blank(info.recipient)) needs.push_back("recipient");
        break;
    case DocKind::Text:
        if (blank(info.label)) needs.push_back("a label");
        switch (info.textStyle) {
        case TextStyle::Single:
            if (blank(info.sender)) needs.push_back("sender");
            if (blank(info.recipient)) needs.push_back("recipient");
            break;
        case TextStyle::Conversation:
            if (blank(info.sender)) needs.push_back("both people");
            else if (blank(info.recipient)) needs.push_back("the second person");
            break;
        case TextStyle::Group:
            if (blank(info.sender)) needs.push_back("the people or group name");
            break;
        }
        break;
    case DocKind::Letter:
        if (blank(info.label)) needs.push_back("a label");
        if (blank(info.sender)) needs.push_back("sender");
        break;
    case DocKind::Other:
        if (blank(info.label)) needs.push_back("a label");
        break;
    }
    if (dateMissing) needs.push_back("a date");
    if (needs.empty()) return std::nullopt;
    return joinNeeds(needs);
}

DateModes allowedDateModes(const DocumentInfo& info)
{
    switch (info.kind) {
    case DocKind::Statement: return {true, true, true};
    case DocKind::Email:
    case DocKind::Letter: return {true, false, false};
    case DocKind::Text: return {true, info.textStyle != TextStyle::Single, false};
    case DocKind::Other: return {true, true, false};
    }
    return {};
}

std::optional<std::string> validateDocumentDate(const DocumentInfo& info, const DateSpec& date)
{
    const DateModes modes = allowedDateModes(info);
    if (std::holds_alternative<Period>(date) && !modes.period) return std::string("This kind of file takes a single date, not a period.");
    if (std::holds_alternative<Quarter>(date) && !modes.quarter) return std::string("Only statements can use a quarter.");
    return std::nullopt;
}

}  // namespace finrenamer
